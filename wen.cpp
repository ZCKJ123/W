// SPDX-License-Identifier: MIT
// 文语言（wen）· 内置签名 WEN-LANG-0.1 · 2026 · 作者与贡献者
// wen.cpp —— 「文」语言 C++17 解释器
// 目标：不经过 Python，直接编译成原生 wen.exe；中文关键字；内置数学库。
// 构建：C:\msys64\ucrt64\bin\g++.exe -std=c++17 -O2 -static -municode -o wen.exe wen.cpp -lwinhttp

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <conio.h>
#include <io.h>

// 源码自带标签：任何二进制都能用 --签名 打印出来。
static const char* WEN_SIGNATURE =
    "文语言（wen） · 内置签名 WEN-LANG-0.1 · 2026 · 作者 Akm / 筑宸科技";

// 永不执行的标记字符串：强制留在二进制里，用来证明源码归属。
__attribute__((used)) static const char WEN_EMBED_MARK[] =
    "WEN-EMBEDDED-MARK · 文语言 v0.1 · 作者 Akm / 筑宸科技 · 2026-10-25 首次发布";
#endif

using std::map;
using std::set;
using std::shared_ptr;
using std::string;
using std::vector;
namespace fs = std::filesystem;

// ============================================================
// 0. 基础工具
// ============================================================

static size_t utf8CharLen(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static vector<string> utf8Chars(const string& s) {
    vector<string> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t n = utf8CharLen((unsigned char)s[i]);
        if (i + n > s.size()) n = 1;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

static size_t utf8Length(const string& s) {
    size_t n = 0, i = 0;
    while (i < s.size()) {
        size_t k = utf8CharLen((unsigned char)s[i]);
        if (i + k > s.size()) k = 1;
        i += k;
        ++n;
    }
    return n;
}

static bool startsWith(const string& s, size_t pos, const string& lit) {
    return pos + lit.size() <= s.size() && s.compare(pos, lit.size(), lit) == 0;
}

// 中文标点/数学符号不能算进标识符里。
// 否则「打印（…）」会被整个当成一个名字，中文代码看起来“语法通过”，其实没执行。
static bool isMultiByteDelimiter(const string& s, size_t pos) {
    static const char* lits[] = {
        "，", "：", "；", "（", "）", "【", "】", "。",
        "＋", "－", "＊", "／", "％", "＝", "＜", "＞", "！",
        "｛", "｝", "［", "］",
        "“", "”", "‘", "’", "「", "」",
        "×", "÷", "·", "⋅", "−", "≤", "≥", "≠", "√", "°",
        "⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹",
        "＆", "｜"
    };
    for (const char* lit : lits) {
        if (startsWith(s, pos, lit)) return true;
    }
    return false;
}

static string trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static void stripBom(string& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
}

// ============================================================
// 1. 错误
// ============================================================

struct WenError : std::runtime_error {
    string kind;
    int line;
    int col;
    vector<string> frames;

    WenError(string kind_, int line_, int col_, string msg)
        : std::runtime_error(std::move(msg)),
          kind(std::move(kind_)),
          line(line_),
          col(col_) {}
};

struct IncompleteError : std::runtime_error {
    int line;
    explicit IncompleteError(int line_, string msg)
        : std::runtime_error(std::move(msg)), line(line_) {}
};

struct IoError : std::runtime_error {
    explicit IoError(string msg) : std::runtime_error(std::move(msg)) {}
};

// ============================================================
// 2. 词法
// ============================================================

enum class TT { End, Newline, Number, String, Template, Name, Keyword, Op };

struct Token {
    TT type = TT::End;
    string text;
    double num = 0;
    int line = 1;
    int col = 1;
};

static const std::set<string>& keywords() {
    static const std::set<string> kw = {
        "将", "变成", "打印", "如果", "则", "否则如果", "否则", "结束",
        "当", "对于", "在", "中", "函数", "返回", "依赖",
        "中断", "继续", "真", "假", "空", "且", "或", "非", "导出",
        "尝试", "捕获", "最终"
    };
    return kw;
}

static bool isIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 0x80;
}

static bool isIdentCont(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

static string describeToken(const Token& t) {
    switch (t.type) {
        case TT::End: return "文件结束";
        case TT::Newline: return "换行";
        case TT::Number: return "数字 " + t.text;
        case TT::String: return "文本";
        case TT::Template: return "插值文本";
        case TT::Name: return "名字「" + t.text + "」";
        case TT::Keyword: return "「" + t.text + "」";
        case TT::Op: return "「" + t.text + "」";
    }
    return "未知";
}

class Lexer {
public:
    explicit Lexer(string src, int startLine = 1)
        : src_(std::move(src)), line_(startLine) {}

    vector<Token> scan() {
        // 去掉 UTF-8 BOM
        if (startsWith(src_, 0, "\xEF\xBB\xBF")) {
            pos_ = 3;
        }
        while (pos_ < src_.size()) {
            unsigned char c = (unsigned char)src_[pos_];

            if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
                ++col_;
                continue;
            }

            if (c == '\n') {
                int ln = line_, cl = col_;
                ++pos_;
                ++line_;
                col_ = 1;
                // 空行算换行；纯注释行不算空行，避免注释头被误判成连续空行。
                if (depth_ == 0 && (lineHasToken_ || !lineHadComment_)) {
                    add(TT::Newline, "\n", ln, cl);
                }
                lineHasToken_ = false;
                lineHadComment_ = false;
                continue;
            }

            if (c == '#') {
                lineHadComment_ = true;
                while (pos_ < src_.size() && src_[pos_] != '\n') {
                    ++pos_;
                    ++col_;
                }
                continue;
            }

            // 插值文本：f"你好，{名字}"
            if (c == 'f' && pos_ + 1 < src_.size()) {
                char n = src_[pos_ + 1];
                if (n == '"' || n == '\'') {
                    string q(1, n);
                    scanTemplate(q, q);
                    continue;
                }
                if (startsWith(src_, pos_ + 1, "“")) { scanTemplate("“", "”"); continue; }
                if (startsWith(src_, pos_ + 1, "‘")) { scanTemplate("‘", "’"); continue; }
                if (startsWith(src_, pos_ + 1, "「")) { scanTemplate("「", "」"); continue; }
            }

            // 文本
            if (c == '"' || c == '\'') {
                string q(1, (char)c);
                scanString(q, q);
                continue;
            }
            if (startsWith(src_, pos_, "“")) { scanString("“", "”"); continue; }
            if (startsWith(src_, pos_, "‘")) { scanString("‘", "’"); continue; }
            if (startsWith(src_, pos_, "「")) { scanString("「", "」"); continue; }
            if (startsWith(src_, pos_, "”") || startsWith(src_, pos_, "’") ||
                startsWith(src_, pos_, "」")) {
                throw WenError("词法错误", line_, col_, "多余的右引号");
            }

            // 中文/数学符号
            if (tryUnicodeMath()) continue;
            if (tryFullWidth()) continue;

            if (std::isdigit(c)) {
                scanNumber();
                continue;
            }

            if (scanOperator()) continue;

            if (isMultiByteDelimiter(src_, pos_)) {
                throw WenError("词法错误", line_, col_, "不认识的符号");
            }

            if (isIdentStart(c)) {
                scanName();
                continue;
            }

            throw WenError("词法错误", line_, col_,
                           string("不认识的字符：") + (char)c);
        }
        add(TT::End, "", line_, col_);
        return std::move(out_);
    }

private:
    string src_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    int depth_ = 0;
    bool lineHasToken_ = false;
    bool lineHadComment_ = false;
    vector<Token> out_;

    void add(TT type, string text, int ln, int cl, double num = 0) {
        Token t;
        t.type = type;
        t.text = std::move(text);
        t.line = ln;
        t.col = cl;
        t.num = num;
        if (type != TT::Newline) lineHasToken_ = true;
        out_.push_back(std::move(t));
    }

    bool tryUnicodeMath() {
        struct Map { const char* lit; const char* op; };
        static const Map maps[] = {
            {"×", "*"}, {"÷", "/"}, {"·", "*"}, {"⋅", "*"}, {"−", "-"},
            {"≤", "<="}, {"≥", ">="}, {"≠", "!="},
            {"√", "√"}, {"°", "°"},
            {"⁰", "⁰"}, {"¹", "¹"}, {"²", "²"}, {"³", "³"},
            {"⁴", "⁴"}, {"⁵", "⁵"}, {"⁶", "⁶"}, {"⁷", "⁷"},
            {"⁸", "⁸"}, {"⁹", "⁹"},
        };
        for (const auto& m : maps) {
            if (startsWith(src_, pos_, m.lit)) {
                add(TT::Op, m.op, line_, col_);
                pos_ += string(m.lit).size();
                ++col_;
                return true;
            }
        }
        if (startsWith(src_, pos_, "＆＆")) {
            add(TT::Op, "&&", line_, col_);
            pos_ += string("＆＆").size();
            col_ += 2;
            return true;
        }
        if (startsWith(src_, pos_, "｜｜")) {
            add(TT::Op, "||", line_, col_);
            pos_ += string("｜｜").size();
            col_ += 2;
            return true;
        }
        return false;
    }

    bool tryFullWidth() {
        struct Map { const char* lit; const char* op; };
        static const Map maps[] = {
            {"，", ","}, {"：", ":"}, {"；", ";"},
            {"（", "("}, {"）", ")"},
            {"【", "["}, {"】", "]"},
            {"＋", "+"}, {"－", "-"}, {"＊", "*"}, {"／", "/"}, {"％", "%"},
            {"＝", "="}, {"＜", "<"}, {"＞", ">"}, {"！", "!"},
            {"｛", "{"}, {"｝", "}"}, {"［", "["}, {"］", "]"},
        };
        for (const auto& m : maps) {
            if (startsWith(src_, pos_, m.lit)) {
                string op = m.op;
                add(TT::Op, op, line_, col_);
                pos_ += string(m.lit).size();
                ++col_;
                if (op == "(" || op == "[" || op == "{") ++depth_;
                else if (op == ")" || op == "]" || op == "}") depth_ = std::max(0, depth_ - 1);
                return true;
            }
        }
        if (startsWith(src_, pos_, "。")) {
            int ln = line_, cl = col_;
            add(TT::Op, ";", ln, cl);
            pos_ += string("。").size();
            ++col_;
            return true;
        }
        return false;
    }

    bool scanOperator() {
        static const char* three[] = {"//=", "**="};
        for (const char* op : three) {
            if (startsWith(src_, pos_, op)) {
                add(TT::Op, op, line_, col_);
                pos_ += 3;
                col_ += 3;
                return true;
            }
        }
        static const char* two[] = {"+=", "-=", "*=", "/=", "%=",
                                    "**", "//", "==", "!=", "<=", ">=", "&&", "||"};
        for (const char* op : two) {
            if (startsWith(src_, pos_, op)) {
                add(TT::Op, op, line_, col_);
                pos_ += 2;
                col_ += 2;
                return true;
            }
        }
        static const string one = "+-*/%=<>!()[]{},:;";
        char c = src_[pos_];
        if (one.find(c) != string::npos) {
            string op(1, c);
            add(TT::Op, op, line_, col_);
            ++pos_;
            ++col_;
            if (op == "(" || op == "[" || op == "{") ++depth_;
            else if (op == ")" || op == "]" || op == "}") depth_ = std::max(0, depth_ - 1);
            return true;
        }
        return false;
    }

    void scanNumber() {
        int ln = line_, cl = col_;
        size_t start = pos_;
        while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) {
            ++pos_; ++col_;
        }
        if (pos_ + 1 < src_.size() && src_[pos_] == '.' &&
            std::isdigit((unsigned char)src_[pos_ + 1])) {
            ++pos_; ++col_;
            while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) {
                ++pos_; ++col_;
            }
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            size_t save = pos_;
            int saveCol = col_;
            ++pos_; ++col_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
                ++pos_; ++col_;
            }
            bool hasDigit = false;
            while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) {
                ++pos_; ++col_;
                hasDigit = true;
            }
            if (!hasDigit) {
                pos_ = save;
                col_ = saveCol;
            }
        }
        string text = src_.substr(start, pos_ - start);
        double num = 0;
        try {
            num = std::stod(text);
        } catch (...) {
            throw WenError("词法错误", ln, cl, "数字格式不对：" + text);
        }
        add(TT::Number, text, ln, cl, num);
    }

    void scanName() {
        int ln = line_, cl = col_;
        size_t start = pos_;
        while (pos_ < src_.size() &&
               isIdentCont((unsigned char)src_[pos_])) {
            if (isMultiByteDelimiter(src_, pos_)) break;
            size_t n = utf8CharLen((unsigned char)src_[pos_]);
            if (pos_ + n > src_.size()) n = 1;
            pos_ += n;
            ++col_;
        }
        string word = src_.substr(start, pos_ - start);
        if (word.empty()) {
            throw WenError("词法错误", line_, col_, "不认识的符号");
        }

        static const std::map<string, string> wordOps = {
            {"加", "+"}, {"加上", "+"}, {"减", "-"}, {"减去", "-"},
            {"乘", "*"}, {"乘以", "*"}, {"除", "/"}, {"除以", "/"},
            {"等于", "=="}, {"不等于", "!="},
            {"小于", "<"}, {"小于等于", "<="},
            {"大于", ">"}, {"大于等于", ">="},
        };
        auto oit = wordOps.find(word);
        if (oit != wordOps.end()) {
            add(TT::Op, oit->second, ln, cl);
            return;
        }
        if (word == "平方" || word == "的平方") {
            add(TT::Op, "²", ln, cl);
            return;
        }
        if (word == "立方" || word == "的立方") {
            add(TT::Op, "³", ln, cl);
            return;
        }
        if (word == "根号") {
            add(TT::Op, "√", ln, cl);
            return;
        }
        if (word == "度") {
            add(TT::Op, "°", ln, cl);
            return;
        }
        // 「甲的平方」不用空格也能写：拆成「甲」+「²」
        for (const string& suf : {string("的平方"), string("的立方")}) {
            if (word.size() > suf.size() &&
                word.compare(word.size() - suf.size(), suf.size(), suf) == 0) {
                string prefix = word.substr(0, word.size() - suf.size());
                add(TT::Name, prefix, ln, cl);
                add(TT::Op, suf == "的平方" ? "²" : "³", ln, cl);
                return;
            }
        }

        if (keywords().count(word)) add(TT::Keyword, word, ln, cl);
        else add(TT::Name, word, ln, cl);
    }

    void scanString(const string& open, const string& close) {
        // 多行文本："""…""" 或 '''…'''，原样保留换行。
        string triple = open + open + open;
        if (startsWith(src_, pos_, triple)) {
            int ln = line_, cl = col_;
            pos_ += triple.size();
            col_ += 3;
            string val;
            while (pos_ < src_.size()) {
                if (startsWith(src_, pos_, triple)) {
                    pos_ += triple.size();
                    col_ += 3;
                    add(TT::String, val, ln, cl);
                    return;
                }
                char c = src_[pos_];
                if (c == '\n') {
                    val += '\n';
                    ++pos_;
                    ++line_;
                    col_ = 1;
                    continue;
                }
                size_t n = utf8CharLen((unsigned char)c);
                if (pos_ + n > src_.size()) n = 1;
                val.append(src_, pos_, n);
                pos_ += n;
                ++col_;
            }
            throw WenError("词法错误", ln, cl, "多行文本没有闭合（缺少 \"\"\"）");
        }
        int ln = line_, cl = col_;
        pos_ += open.size();
        ++col_;
        string val;
        while (pos_ < src_.size()) {
            if (startsWith(src_, pos_, close)) {
                pos_ += close.size();
                ++col_;
                add(TT::String, val, ln, cl);
                return;
            }
            char c = src_[pos_];
            if (c == '\n') {
                throw WenError("词法错误", line_, col_,
                               "文本没有闭合（文本不能跨行）");
            }
            if (c == '\\') {
                if (pos_ + 1 >= src_.size()) {
                    throw WenError("词法错误", line_, col_, "文本末尾的转义不完整");
                }
                char e = src_[pos_ + 1];
                switch (e) {
                    case 'n': val += '\n'; break;
                    case 't': val += '\t'; break;
                    case 'r': val += '\r'; break;
                    case '\\': val += '\\'; break;
                    case '"': val += '"'; break;
                    case '\'': val += '\''; break;
                    default:
                        val += '\\';
                        val += e;
                        break;
                }
                pos_ += 2;
                col_ += 2;
                continue;
            }
            size_t n = utf8CharLen((unsigned char)c);
            if (pos_ + n > src_.size()) n = 1;
            val.append(src_, pos_, n);
            pos_ += n;
            ++col_;
        }
        throw WenError("词法错误", ln, cl, "文本没有闭合");
    }

    // 插值文本的正文：花括号里可以写表达式，里面的引号不结束外层文本。
    string scanTemplateValue(const string& open, const string& close) {
        int ln = line_, cl = col_;
        pos_ += open.size();
        ++col_;
        string val;
        int depth = 0;
        while (pos_ < src_.size()) {
            if (depth == 0 && startsWith(src_, pos_, close)) {
                pos_ += close.size();
                ++col_;
                return val;
            }
            char c = src_[pos_];
            if (c == '\n')
                throw WenError("词法错误", line_, col_, "插值文本不能跨行");
            if (c == '{') {
                ++depth;
                val += c;
                ++pos_;
                ++col_;
                continue;
            }
            if (c == '}' && depth > 0) {
                --depth;
                val += c;
                ++pos_;
                ++col_;
                continue;
            }
            if (c == '\\') {
                if (pos_ + 1 >= src_.size())
                    throw WenError("词法错误", line_, col_, "文本末尾的转义不完整");
                char e = src_[pos_ + 1];
                switch (e) {
                    case 'n': val += '\n'; break;
                    case 't': val += '\t'; break;
                    case 'r': val += '\r'; break;
                    case '\\': val += '\\'; break;
                    case '"': val += '"'; break;
                    case '\'': val += '\''; break;
                    default:
                        val += '\\';
                        val += e;
                        break;
                }
                pos_ += 2;
                col_ += 2;
                continue;
            }
            size_t n = utf8CharLen((unsigned char)c);
            if (pos_ + n > src_.size()) n = 1;
            val.append(src_, pos_, n);
            pos_ += n;
            ++col_;
        }
        throw WenError("词法错误", ln, cl, "插值文本没有闭合");
    }

    void scanTemplate(const string& open, const string& close) {
        int ln = line_, cl = col_;
        ++pos_;
        ++col_;
        string val = scanTemplateValue(open, close);
        add(TT::Template, val, ln, cl);
    }
};

// ============================================================
// 3. AST
// ============================================================

struct Expr;
using ExprPtr = shared_ptr<Expr>;

struct Expr {
    enum class K { Num, Str, Bool, None, Var, Unary, Binary, Call, Index, Slice, List, Dict };
    K k = K::None;
    double num = 0;
    string text;
    bool b = false;
    bool commandCall = false;
    string op;
    vector<ExprPtr> args;
    vector<std::pair<ExprPtr, ExprPtr>> pairs;
    int line = 0;
};

struct Stmt;
using StmtPtr = shared_ptr<Stmt>;

struct Stmt {
    enum class K {
        Assign, Print, If, While, For, Func, Return,
        Break, Continue, ExprStmt, Import, Export, Try
    };
    K k = K::ExprStmt;
    string name;
    string name2;   // 「对于 键, 值 在 字典 中」的第二个变量
    ExprPtr target;
    ExprPtr expr;
    vector<ExprPtr> args;
    vector<StmtPtr> body;
    vector<StmtPtr> elseBody;
    vector<StmtPtr> finallyBody;
    vector<std::pair<ExprPtr, vector<StmtPtr>>> branches;
    vector<string> params;
    vector<string> imports;
    int line = 0;
};

static ExprPtr mkNum(double v, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Num;
    e->num = v;
    e->line = line;
    return e;
}
static ExprPtr mkStr(string v, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Str;
    e->text = std::move(v);
    e->line = line;
    return e;
}
static ExprPtr mkBool(bool v, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Bool;
    e->b = v;
    e->line = line;
    return e;
}
static ExprPtr mkNone(int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::None;
    e->line = line;
    return e;
}
static ExprPtr mkVar(string name, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Var;
    e->text = std::move(name);
    e->line = line;
    return e;
}
static ExprPtr mkUnary(string op, ExprPtr a, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Unary;
    e->op = std::move(op);
    e->args.push_back(std::move(a));
    e->line = line;
    return e;
}
static ExprPtr mkBinary(string op, ExprPtr a, ExprPtr b, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Binary;
    e->op = std::move(op);
    e->args.push_back(std::move(a));
    e->args.push_back(std::move(b));
    e->line = line;
    return e;
}
static ExprPtr mkCall(ExprPtr callee, vector<ExprPtr> args, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Call;
    e->args.push_back(std::move(callee));
    for (auto& a : args) e->args.push_back(std::move(a));
    if (e->args[0]->k == Expr::K::Var && e->args[0]->text == "指挥") e->commandCall = true;
    e->line = line;
    return e;
}
static ExprPtr mkIndex(ExprPtr obj, ExprPtr idx, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Index;
    e->args.push_back(std::move(obj));
    e->args.push_back(std::move(idx));
    e->line = line;
    return e;
}
// 切片：obj[起:止:步]，三个参数都可以缺省（用 nullptr 表示）。
static ExprPtr mkSlice(ExprPtr obj, ExprPtr start, ExprPtr stop, ExprPtr step, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Slice;
    e->args.push_back(std::move(obj));
    e->args.push_back(std::move(start));
    e->args.push_back(std::move(stop));
    e->args.push_back(std::move(step));
    e->line = line;
    return e;
}
static ExprPtr mkList(vector<ExprPtr> items, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::List;
    e->args = std::move(items);
    e->line = line;
    return e;
}
static ExprPtr mkDict(vector<std::pair<ExprPtr, ExprPtr>> pairs, int line) {
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Dict;
    e->pairs = std::move(pairs);
    e->line = line;
    return e;
}

// ============================================================
// 4. 语法
// ============================================================

static bool startsCallArg(const Token& t) {
    if (t.type == TT::Number || t.type == TT::String || t.type == TT::Name) return true;
    if (t.type == TT::Keyword &&
        (t.text == "真" || t.text == "假" || t.text == "空")) {
        return true;
    }
    if (t.type == TT::Op && (t.text == "[" || t.text == "√")) return true;
    return false;
}

static int superscriptDigit(const string& s) {
    if (s == "⁰") return 0;
    if (s == "¹") return 1;
    if (s == "²") return 2;
    if (s == "³") return 3;
    if (s == "⁴") return 4;
    if (s == "⁵") return 5;
    if (s == "⁶") return 6;
    if (s == "⁷") return 7;
    if (s == "⁸") return 8;
    if (s == "⁹") return 9;
    return -1;
}

class Parser {
public:
    explicit Parser(vector<Token> tokens, bool recover = false)
        : tokens_(std::move(tokens)), recover_(recover) {}

    const vector<WenError>& errors() const { return errors_; }

    vector<StmtPtr> parseProgram() {
        try {
            skipSeparators();
        } catch (WenError& e) {
            if (!recover_) throw;
            errors_.push_back(e);
            recoverToBoundary();
        }
        vector<StmtPtr> body;
        while (!at(TT::End)) {
            size_t before = idx_;
            try {
                body.push_back(parseStatement());
                skipSeparators();
            } catch (WenError& e) {
                if (!recover_) throw;
                errors_.push_back(e);
                recoverToBoundary();
                if (idx_ == before) ++idx_;
            } catch (IncompleteError& e) {
                if (!recover_) throw;
                errors_.push_back(WenError("语法错误", e.line, 0, e.what()));
                break;
            }
        }
        return body;
    }

private:
    vector<Token> tokens_;
    size_t idx_ = 0;
    bool recover_ = false;
    vector<WenError> errors_;

    const Token& peek(int k = 0) const {
        size_t i = idx_ + (size_t)k;
        if (i >= tokens_.size()) return tokens_.back();
        return tokens_[i];
    }
    bool at(TT t) const { return peek().type == t; }
    bool atKw(const string& s) const {
        return peek().type == TT::Keyword && peek().text == s;
    }
    bool atOp(const string& s) const {
        return peek().type == TT::Op && peek().text == s;
    }
    bool matchKw(const string& s) {
        if (atKw(s)) { ++idx_; return true; }
        return false;
    }
    bool matchOp(const string& s) {
        if (atOp(s)) { ++idx_; return true; }
        return false;
    }

    [[noreturn]] void err(const Token& t, const string& msg) const {
        throw WenError("语法错误", t.line, t.col, msg);
    }
    [[noreturn]] void incomplete(const Token& t, const string& msg) const {
        throw IncompleteError(t.line, msg);
    }

    Token expectKw(const string& s) {
        if (atKw(s)) return tokens_[idx_++];
        if (at(TT::End)) incomplete(peek(), "缺少「" + s + "」");
        err(peek(), "期望「" + s + "」，却遇到" + describeToken(peek()));
    }
    Token expectOp(const string& s) {
        if (atOp(s)) return tokens_[idx_++];
        if (at(TT::End)) incomplete(peek(), "缺少「" + s + "」");
        err(peek(), "期望「" + s + "」，却遇到" + describeToken(peek()));
    }
    Token expectName() {
        if (at(TT::Name)) return tokens_[idx_++];
        if (at(TT::End)) incomplete(peek(), "这里需要一个名字");
        if (at(TT::Op)) {
            string op = peek().text;
            string word = op;
            if (op == "+") word = "加";
            else if (op == "-") word = "减";
            else if (op == "*") word = "乘";
            else if (op == "/") word = "除";
            err(peek(), "这里需要一个名字，却遇到运算符「" + word +
                            "」；运算符不能当名字，请改名（例如「相加」）");
        }
        if (at(TT::Keyword))
            err(peek(), "这里需要一个名字，却遇到关键字「" + peek().text +
                            "」；关键字不能当名字，请改名");
        err(peek(), "这里需要一个名字，却遇到" + describeToken(peek()));
    }

    void skipNewlines() {
        while (at(TT::Newline)) ++idx_;
    }
    void skipSeparators() {
        // 语句之间可以空一行，不能空两行。
        // 一个空行会产生 2 个换行 token（语句行尾 + 空行本身），
        // 连续 3 个换行才说明真的空了两行。
        int newlines = 0;
        while (at(TT::Newline) || atOp(";")) {
            if (at(TT::Newline)) {
                ++newlines;
                if (newlines >= 3) {
                    err(peek(),
                        "这里连续空了两行。本语言规定语句之间最多空一行；请删掉多余的空行");
                }
            } else {
                newlines = 0;
            }
            ++idx_;
        }
    }

    bool atStmtEnd() const {
        if (at(TT::Newline) || at(TT::End)) return true;
        if (atOp(";")) return true;
        if (peek().type == TT::Keyword &&
            (peek().text == "结束" || peek().text == "否则" || peek().text == "否则如果")) {
            return true;
        }
        return false;
    }

    void endStmt() {
        if (at(TT::Newline) || atOp(";")) {
            skipSeparators();
            return;
        }
        if (at(TT::End)) return;
        if (peek().type == TT::Keyword &&
            (peek().text == "结束" || peek().text == "否则" || peek().text == "否则如果")) {
            return;
        }
        err(peek(), "语句之间需要换行或分号");
    }

    vector<StmtPtr> parseBlock(const std::set<string>& terminators) {
        vector<StmtPtr> body;
        try {
            skipSeparators();
        } catch (WenError& e) {
            if (!recover_) throw;
            errors_.push_back(e);
            recoverToBoundary();
        }
        while (!at(TT::End)) {
            if (peek().type == TT::Keyword && terminators.count(peek().text)) break;
            size_t before = idx_;
            try {
                body.push_back(parseStatement());
                skipSeparators();
            } catch (WenError& e) {
                if (!recover_) throw;
                errors_.push_back(e);
                recoverToBoundary();
                if (idx_ == before) ++idx_;
            } catch (IncompleteError& e) {
                if (!recover_) throw;
                errors_.push_back(WenError("语法错误", e.line, 0, e.what()));
                break;
            }
        }
        if (at(TT::End) && !recover_) {
            incomplete(peek(), "块没有闭合，缺少「结束」");
        }
        return body;
    }

    void recoverToBoundary() {
        if (at(TT::End)) return;
        // 用行号恢复：即使漏了右括号、词法器不再产出换行 token，
        // 也能在下一行开头继续，不会一口气跳到文件末尾。
        int startLine = peek().line;
        while (!at(TT::End)) {
            if (peek().line > startLine) return;
            if (peek().type == TT::Keyword &&
                (peek().text == "结束" || peek().text == "否则" ||
                 peek().text == "否则如果")) {
                return;
            }
            ++idx_;
        }
    }

    StmtPtr parseStatement() {
        if (atKw("将")) return parseAssign();
        if (atKw("打印")) return parsePrint();
        if (atKw("如果")) return parseIf();
        if (atKw("当")) return parseWhile();
        if (atKw("对于")) return parseFor();
        if (atKw("函数")) return parseFunc();
        if (atKw("返回")) return parseReturn();
        if (atKw("中断")) return parseBreak();
        if (atKw("继续")) return parseContinue();
        if (atKw("依赖")) return parseImport();
        if (atKw("导出")) return parseExport();
        if (atKw("尝试")) return parseTry();
        return parseExprStatement();
    }

    StmtPtr parseAssign() {
        int line = peek().line;
        ++idx_;
        Token nm = expectName();
        ExprPtr target = mkVar(nm.text, nm.line);
        while (matchOp("[")) target = parseIndexPart(target, nm.line);
        static const std::set<string> compoundOps = {
            "+=", "-=", "*=", "/=", "//=", "%=", "**="};
        ExprPtr val;
        if (at(TT::Op) && compoundOps.count(peek().text)) {
            string op = peek().text.substr(0, peek().text.size() - 1);
            ++idx_;
            ExprPtr rhs = parseExpr();
            val = mkBinary(op, target, rhs, line);
        } else {
            expectKw("变成");
            val = parseExpr();
        }
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Assign;
        s->target = target;
        s->expr = val;
        s->line = line;
        return s;
    }

    // 解析 `[` 后面的部分：普通下标或切片，最后吃掉 `]`。
    ExprPtr parseIndexPart(ExprPtr obj, int line) {
        ExprPtr first;
        bool hasFirst = false;
        if (!atOp(":") && !atOp("]")) {
            first = parseExpr();
            hasFirst = true;
        }
        if (atOp(":")) {
            ++idx_;
            ExprPtr stop, step;
            if (!atOp("]") && !atOp(":")) stop = parseExpr();
            if (atOp(":")) {
                ++idx_;
                if (!atOp("]")) step = parseExpr();
            }
            expectOp("]");
            return mkSlice(obj, hasFirst ? first : nullptr, stop, step, line);
        }
        if (!hasFirst) err(peek(), "下标不能为空");
        expectOp("]");
        return mkIndex(obj, first, line);
    }

    StmtPtr parsePrint() {
        int line = peek().line;
        ++idx_;
        vector<ExprPtr> args;
        if (matchOp("(")) {
            if (!atOp(")")) {
                args.push_back(parseExpr());
                while (matchOp(",")) {
                    if (atOp(")")) break;
                    args.push_back(parseExpr());
                }
            }
            expectOp(")");
        } else {
            if (!atStmtEnd()) {
                args.push_back(parseExpr());
                while (matchOp(",")) args.push_back(parseExpr());
            }
        }
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Print;
        s->args = std::move(args);
        s->line = line;
        return s;
    }

    StmtPtr parseIf() {
        int line = peek().line;
        ++idx_;
        ExprPtr cond = parseExpr();
        expectKw("则");
        vector<StmtPtr> body = parseBlock({"否则如果", "否则", "结束"});
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::If;
        s->line = line;
        s->branches.push_back({cond, body});

        while (matchKw("否则如果")) {
            ExprPtr c = parseExpr();
            expectKw("则");
            auto b = parseBlock({"否则如果", "否则", "结束"});
            s->branches.push_back({c, b});
        }
        if (matchKw("否则")) {
            s->elseBody = parseBlock({"结束"});
        }
        expectKw("结束");
        endStmt();
        return s;
    }

    StmtPtr parseWhile() {
        int line = peek().line;
        ++idx_;
        ExprPtr cond = parseExpr();
        expectKw("则");
        auto body = parseBlock({"结束"});
        expectKw("结束");
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::While;
        s->expr = cond;
        s->body = body;
        s->line = line;
        return s;
    }

    StmtPtr parseFor() {
        int line = peek().line;
        ++idx_;
        Token nm = expectName();
        string name2;
        if (matchOp(",")) name2 = expectName().text;
        expectKw("在");
        ExprPtr iter = parseExpr();
        expectKw("中");
        expectKw("则");
        auto body = parseBlock({"结束"});
        expectKw("结束");
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::For;
        s->name = nm.text;
        s->name2 = name2;
        s->expr = iter;
        s->body = body;
        s->line = line;
        return s;
    }

    StmtPtr parseTry() {
        int line = peek().line;
        ++idx_;
        auto body = parseBlock({"捕获", "最终", "结束"});
        string catchName;
        vector<StmtPtr> catchBody, finallyBody;
        if (matchKw("捕获")) {
            catchName = expectName().text;
            catchBody = parseBlock({"最终", "结束"});
        }
        if (matchKw("最终")) finallyBody = parseBlock({"结束"});
        if (catchName.empty() && finallyBody.empty())
            err(peek(), "「尝试」后面要跟「捕获」或「最终」");
        expectKw("结束");
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Try;
        s->body = body;
        s->name = catchName;
        s->elseBody = catchBody;
        s->finallyBody = finallyBody;
        s->line = line;
        return s;
    }

    StmtPtr parseFunc() {
        int line = peek().line;
        ++idx_;
        Token nm = expectName();
        vector<string> params;
        if (matchOp("(")) {
            // 旧写法：函数 f(x) 则
            if (!atOp(")")) {
                params.push_back(expectName().text);
                while (matchOp(",")) {
                    if (atOp(")")) break;
                    params.push_back(expectName().text);
                }
            }
            expectOp(")");
        } else {
            // 新写法：函数 f x, y 则
            while (!atKw("则")) {
                params.push_back(expectName().text);
                matchOp(",");
            }
        }
        expectKw("则");
        auto body = parseBlock({"结束"});
        expectKw("结束");
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Func;
        s->name = nm.text;
        s->params = std::move(params);
        s->body = body;
        s->line = line;
        return s;
    }

    StmtPtr parseReturn() {
        int line = peek().line;
        ++idx_;
        ExprPtr val;
        if (!atStmtEnd()) {
            val = parseExpr();
        }
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Return;
        s->expr = val;
        s->line = line;
        return s;
    }

    StmtPtr parseBreak() {
        int line = peek().line;
        ++idx_;
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Break;
        s->line = line;
        return s;
    }

    StmtPtr parseContinue() {
        int line = peek().line;
        ++idx_;
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Continue;
        s->line = line;
        return s;
    }

    StmtPtr parseImport() {
        int line = peek().line;
        ++idx_;
        vector<string> names;
        names.push_back(expectName().text);
        while (true) {
            if (matchOp(",")) {
                names.push_back(expectName().text);
                continue;
            }
            if (at(TT::Name)) {
                names.push_back(expectName().text);
                continue;
            }
            break;
        }
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Import;
        s->imports = std::move(names);
        s->line = line;
        return s;
    }

    StmtPtr parseExport() {
        int line = peek().line;
        ++idx_;
        vector<string> names;
        names.push_back(expectName().text);
        while (true) {
            if (matchOp(",")) {
                names.push_back(expectName().text);
                continue;
            }
            if (at(TT::Name)) {
                names.push_back(expectName().text);
                continue;
            }
            break;
        }
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::Export;
        s->imports = std::move(names);
        s->line = line;
        return s;
    }

    StmtPtr parseExprStatement() {
        int line = peek().line;
        ExprPtr e = parseExpr();
        endStmt();
        auto s = std::make_shared<Stmt>();
        s->k = Stmt::K::ExprStmt;
        s->expr = e;
        s->line = line;
        return s;
    }

    ExprPtr parseExpr() { return parseOr(); }

    ExprPtr parseOr() {
        ExprPtr left = parseAnd();
        while (atKw("或") || atOp("||")) {
            int line = peek().line;
            ++idx_;
            ExprPtr right = parseAnd();
            left = mkBinary("或", left, right, line);
        }
        return left;
    }

    ExprPtr parseAnd() {
        ExprPtr left = parseNot();
        while (atKw("且") || atOp("&&")) {
            int line = peek().line;
            ++idx_;
            ExprPtr right = parseNot();
            left = mkBinary("且", left, right, line);
        }
        return left;
    }

    ExprPtr parseNot() {
        if (atKw("非") || atOp("!")) {
            int line = peek().line;
            ++idx_;
            return mkUnary("非", parseNot(), line);
        }
        return parseComparison();
    }

    ExprPtr parseComparison() {
        ExprPtr left = parseAdd();
        if (peek().type == TT::Op) {
            const string& op = peek().text;
            if (op == "==" || op == "=" || op == "!=" || op == "<" ||
                op == "<=" || op == ">" || op == ">=") {
                int line = peek().line;
                ++idx_;
                ExprPtr right = parseAdd();
                return mkBinary(op, left, right, line);
            }
        }
        if (matchKw("在")) {
            int line = peek().line;
            ExprPtr right = parseAdd();
            expectKw("中");
            return mkBinary("在", left, right, line);
        }
        return left;
    }

    ExprPtr parseAdd() {
        ExprPtr left = parseMul();
        while (atOp("+") || atOp("-")) {
            string op = peek().text;
            int line = peek().line;
            ++idx_;
            ExprPtr right = parseMul();
            left = mkBinary(op, left, right, line);
        }
        return left;
    }

    ExprPtr parseMul() {
        ExprPtr left = parseUnary();
        while (atOp("*") || atOp("/") || atOp("//") || atOp("%")) {
            string op = peek().text;
            int line = peek().line;
            ++idx_;
            ExprPtr right = parseUnary();
            left = mkBinary(op, left, right, line);
        }
        return left;
    }

    ExprPtr parseUnary() {
        if (atOp("√")) {
            int line = peek().line;
            ++idx_;
            ExprPtr v = parseUnary();
            vector<ExprPtr> args{v};
            return mkCall(mkVar("平方根", line), std::move(args), line);
        }
        if (atOp("-")) {
            int line = peek().line;
            ++idx_;
            return mkUnary("-", parseUnary(), line);
        }
        if (atOp("+")) {
            ++idx_;
            return parseUnary();
        }
        return parsePow();
    }

    ExprPtr parsePow() {
        ExprPtr left = parsePostfix();
        if (matchOp("**")) {
            int line = peek().line;
            ExprPtr right = parseUnary();
            return mkBinary("**", left, right, line);
        }
        return left;
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parseAtom();
        while (true) {
            if (peek().type == TT::Op) {
                int d = superscriptDigit(peek().text);
                if (d >= 0) {
                    int line = peek().line;
                    ++idx_;
                    e = mkBinary("**", e, mkNum((double)d, line), line);
                    continue;
                }
                if (atOp("°")) {
                    int line = peek().line;
                    ++idx_;
                    vector<ExprPtr> args{e};
                    e = mkCall(mkVar("角度转弧度", line), std::move(args), line);
                    continue;
                }
            }
            if (matchOp("(")) {
                vector<ExprPtr> args;
                if (!atOp(")")) {
                    args.push_back(parseExpr());
                    while (matchOp(",")) {
                        if (atOp(")")) break;
                        args.push_back(parseExpr());
                    }
                }
                expectOp(")");
                e = mkCall(e, std::move(args), e->line);
                continue;
            }
            if (matchOp("[")) {
                e = parseIndexPart(e, e->line);
                continue;
            }
            // 无括号单参数调用：正弦 30°、平方根 2、圆面积 3
            if (e->k == Expr::K::Var && startsCallArg(peek())) {
                int line = peek().line;
                ExprPtr arg = parseUnary();
                vector<ExprPtr> args{arg};
                e = mkCall(e, std::move(args), line);
                continue;
            }
            break;
        }
        return e;
    }

    ExprPtr parseTemplate(const string& text, int line) {
        ExprPtr acc = mkStr("", line);
        string lit;
        auto flush = [&]() {
            if (!lit.empty()) {
                acc = mkBinary("+", acc, mkStr(lit, line), line);
                lit.clear();
            }
        };
        for (size_t i = 0; i < text.size();) {
            char c = text[i];
            if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') {
                lit += '{';
                i += 2;
                continue;
            }
            if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') {
                lit += '}';
                i += 2;
                continue;
            }
            if (c == '{') {
                size_t j = text.find('}', i + 1);
                if (j == string::npos)
                    throw WenError("语法错误", line, 0, "插值文本缺少「}」");
                string inner = text.substr(i + 1, j - i - 1);
                flush();
                ExprPtr e = parseEmbedded(inner, line);
                vector<ExprPtr> args{e};
                acc = mkBinary("+", acc,
                               mkCall(mkVar("文本", line), std::move(args), line), line);
                i = j + 1;
                continue;
            }
            lit += c;
            ++i;
        }
        flush();
        return acc;
    }

    ExprPtr parseEmbedded(const string& src, int line) {
        try {
            Lexer lx(src, line);
            Parser p(lx.scan());
            return p.parseSingleExpression();
        } catch (WenError& e) {
            throw WenError("语法错误", line, 0, string("插值表达式错误：") + e.what());
        } catch (IncompleteError& e) {
            throw WenError("语法错误", line, 0, string("插值表达式不完整：") + e.what());
        }
    }

    ExprPtr parseSingleExpression() {
        skipNewlines();
        ExprPtr e = parseExpr();
        skipNewlines();
        if (!at(TT::End))
            err(peek(), "插值表达式后面还有多余内容");
        return e;
    }

    ExprPtr parseAtom() {
        Token t = peek();
        if (t.type == TT::Number) {
            ++idx_;
            return mkNum(t.num, t.line);
        }
        if (t.type == TT::String) {
            ++idx_;
            return mkStr(t.text, t.line);
        }
        if (t.type == TT::Template) {
            ++idx_;
            return parseTemplate(t.text, t.line);
        }
        if (t.type == TT::Keyword) {
            if (t.text == "真") { ++idx_; return mkBool(true, t.line); }
            if (t.text == "假") { ++idx_; return mkBool(false, t.line); }
            if (t.text == "空") { ++idx_; return mkNone(t.line); }
        }
        if (t.type == TT::Name) {
            ++idx_;
            return mkVar(t.text, t.line);
        }
        if (matchOp("(")) {
            ExprPtr e = parseExpr();
            expectOp(")");
            return e;
        }
        if (matchOp("[")) {
            vector<ExprPtr> items;
            if (!atOp("]")) {
                items.push_back(parseExpr());
                while (matchOp(",")) {
                    if (atOp("]")) break;
                    items.push_back(parseExpr());
                }
            }
            expectOp("]");
            return mkList(std::move(items), t.line);
        }
        if (matchOp("{")) {
            vector<std::pair<ExprPtr, ExprPtr>> pairs;
            if (!atOp("}")) {
                while (true) {
                    ExprPtr key = parseExpr();
                    expectOp(":");
                    ExprPtr val = parseExpr();
                    pairs.push_back({key, val});
                    if (!matchOp(",")) break;
                    if (atOp("}")) break;
                }
            }
            expectOp("}");
            return mkDict(std::move(pairs), t.line);
        }
        if (t.type == TT::End) {
            incomplete(t, "表达式没有写完");
        }
        err(t, "这里需要一个值，却遇到" + describeToken(t));
    }
};

// ============================================================
// 5. 运行时值
// ============================================================

enum class VT { None, Bool, Num, Str, List, Dict, Func, Set, Bytes, Range };

struct List;
struct Dict;
struct Callable;
class Interpreter;

struct Value {
    VT type = VT::None;
    bool b = false;
    double num = 0;
    string str;
    shared_ptr<List> list;
    shared_ptr<Dict> dict;
    shared_ptr<Callable> callable;
    shared_ptr<vector<unsigned char>> bytes;
    double rstart = 0, rstop = 0, rstep = 1;
};

struct List { vector<Value> items; };
struct Dict { map<string, Value> items; };

struct Scope {
    map<string, Value> vars;
    shared_ptr<Scope> parent;

    Scope() = default;
    explicit Scope(shared_ptr<Scope> p) : parent(std::move(p)) {}

    bool lookup(const string& name, Value& out) const {
        auto it = vars.find(name);
        if (it != vars.end()) { out = it->second; return true; }
        if (parent) return parent->lookup(name, out);
        return false;
    }
};

struct Callable {
    string name;
    virtual ~Callable() = default;
    virtual Value call(Interpreter& vm, vector<Value>& args, int line) = 0;
    virtual string display() const = 0;
    // 零参数函数：名字后面不写括号也直接调用（少括号写法）。
    virtual bool zeroArgCall() const { return false; }
    virtual bool cacheable() const { return false; }
};

struct Function : Callable {
    vector<string> params;
    vector<StmtPtr> body;
    shared_ptr<Scope> closure;
    int line = 0;

    Value call(Interpreter& vm, vector<Value>& args, int line) override;
    string display() const override { return "<函数 " + name + ">"; }
    bool zeroArgCall() const override { return params.empty(); }
};

struct Builtin : Callable {
    int minArgs = 0;
    int maxArgs = -1;
    string module;
    bool moduleKnown = false;
    bool cacheableFn = false;
    std::function<Value(Interpreter&, vector<Value>&, int)> fn;

    Value call(Interpreter& vm, vector<Value>& args, int line) override;
    string display() const override { return "<内置函数 " + name + ">"; }
    bool zeroArgCall() const override { return minArgs == 0; }
    bool cacheable() const override { return cacheableFn; }
};

static bool cacheableBuiltin(const string& name);

static Value vNone() { return Value{}; }
static Value vBool(bool b) { Value v; v.type = VT::Bool; v.b = b; return v; }
static Value vNum(double d) { Value v; v.type = VT::Num; v.num = d; return v; }
static Value vStr(string s) { Value v; v.type = VT::Str; v.str = std::move(s); return v; }
static Value vList() { Value v; v.type = VT::List; v.list = std::make_shared<List>(); return v; }
static Value vList(shared_ptr<List> l) { Value v; v.type = VT::List; v.list = std::move(l); return v; }
static Value vDict(shared_ptr<Dict> d) { Value v; v.type = VT::Dict; v.dict = std::move(d); return v; }
static Value vFunc(shared_ptr<Callable> c) { Value v; v.type = VT::Func; v.callable = std::move(c); return v; }
static Value vSet() { Value v; v.type = VT::Set; v.list = std::make_shared<List>(); return v; }
static Value vSet(shared_ptr<List> l) { Value v; v.type = VT::Set; v.list = std::move(l); return v; }
static Value vBytes(shared_ptr<vector<unsigned char>> b) { Value v; v.type = VT::Bytes; v.bytes = std::move(b); return v; }
static Value vRange(double a, double b, double c) { Value v; v.type = VT::Range; v.rstart = a; v.rstop = b; v.rstep = c; return v; }

// ============================================================
// 6. 值工具
// ============================================================

static string typeName(const Value& v) {
    switch (v.type) {
        case VT::None: return "空";
        case VT::Bool: return "布尔";
        case VT::Num: return "数字";
        case VT::Str: return "文本";
        case VT::List: return "列表";
        case VT::Dict: return "字典";
        case VT::Func: return "函数";
        case VT::Set: return "集合";
        case VT::Bytes: return "缓冲";
        case VT::Range: return "范围";
    }
    return "未知";
}

static bool truthy(const Value& v) {
    switch (v.type) {
        case VT::None: return false;
        case VT::Bool: return v.b;
        case VT::Num: return v.num != 0;
        case VT::Str: return !v.str.empty();
        case VT::List: return v.list && !v.list->items.empty();
        case VT::Dict: return v.dict && !v.dict->items.empty();
        case VT::Func: return true;
        case VT::Set: return v.list && !v.list->items.empty();
        case VT::Bytes: return v.bytes && !v.bytes->empty();
        case VT::Range: return (v.rstep > 0 ? v.rstart < v.rstop : v.rstart > v.rstop);
    }
    return false;
}

static string numberToString(double d) {
    if (std::isnan(d)) return "非数";
    if (std::isinf(d)) return d < 0 ? "负无穷" : "无穷";
    if (d == 0) return "0";
    // 极小/极大的数用科学计数法，别被下面的整数判断吞成 0。
    if (std::fabs(d) < 1e-12 || std::fabs(d) >= 1e15) {
        std::ostringstream oss;
        oss << std::setprecision(6) << std::scientific << d;
        return oss.str();
    }
    double r = std::round(d);
    if (std::fabs(d - r) < 1e-12 && std::fabs(d) < 1e15) {
        return std::to_string((long long)r);
    }
    std::ostringstream oss;
    oss << std::setprecision(16) << std::defaultfloat << d;
    return oss.str();
}

static string quoteText(const string& s) {
    string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    out += "\"";
    return out;
}

static string toText(const Value& v, bool quoteStrings = false, int depth = 0) {
    if (depth > 30) return "...";
    switch (v.type) {
        case VT::None: return "空";
        case VT::Bool: return v.b ? "真" : "假";
        case VT::Num: return numberToString(v.num);
        case VT::Str: return quoteStrings ? quoteText(v.str) : v.str;
        case VT::List: {
            if (!v.list) return "[]";
            string out = "[";
            for (size_t i = 0; i < v.list->items.size(); ++i) {
                if (i) out += ", ";
                out += toText(v.list->items[i], true, depth + 1);
            }
            out += "]";
            return out;
        }
        case VT::Dict: {
            if (!v.dict) return "{}";
            string out = "{";
            bool first = true;
            for (const auto& kv : v.dict->items) {
                if (!first) out += ", ";
                first = false;
                out += quoteText(kv.first);
                out += ": ";
                out += toText(kv.second, true, depth + 1);
            }
            out += "}";
            return out;
        }
        case VT::Func:
            return v.callable ? v.callable->display() : "<函数>";
        case VT::Bytes: {
            size_t n = v.bytes ? v.bytes->size() : 0;
            return "<缓冲 " + std::to_string(n) + " 字节>";
        }
        case VT::Set: {
            if (!v.list || v.list->items.empty()) return "空集";
            string out = "{";
            for (size_t i = 0; i < v.list->items.size(); ++i) {
                if (i) out += ", ";
                out += toText(v.list->items[i], true, depth + 1);
            }
            out += "}";
            return out;
        }
    }
    return "";
}

static bool valueEquals(const Value& a, const Value& b, int depth = 0) {
    if (depth > 30) return false;
    if (a.type == VT::Num && b.type == VT::Num) return a.num == b.num;
    if (a.type != b.type) return false;
    switch (a.type) {
        case VT::None: return true;
        case VT::Bool: return a.b == b.b;
        case VT::Str: return a.str == b.str;
        case VT::List: {
            if (!a.list || !b.list) return a.list == b.list;
            if (a.list->items.size() != b.list->items.size()) return false;
            for (size_t i = 0; i < a.list->items.size(); ++i)
                if (!valueEquals(a.list->items[i], b.list->items[i], depth + 1)) return false;
            return true;
        }
        case VT::Dict: {
            if (!a.dict || !b.dict) return a.dict == b.dict;
            if (a.dict->items.size() != b.dict->items.size()) return false;
            auto ia = a.dict->items.begin();
            auto ib = b.dict->items.begin();
            for (; ia != a.dict->items.end(); ++ia, ++ib) {
                if (ia->first != ib->first) return false;
                if (!valueEquals(ia->second, ib->second, depth + 1)) return false;
            }
            return true;
        }
        case VT::Func: return a.callable == b.callable;
        case VT::Set: {
            if (!a.list || !b.list) return a.list == b.list;
            if (a.list->items.size() != b.list->items.size()) return false;
            for (const auto& item : a.list->items) {
                bool found = false;
                for (const auto& other : b.list->items) {
                    if (valueEquals(item, other, depth + 1)) { found = true; break; }
                }
                if (!found) return false;
            }
            return true;
        }
        case VT::Num: return a.num == b.num;
        case VT::Bytes: {
            if (!a.bytes || !b.bytes) return a.bytes == b.bytes;
            return *a.bytes == *b.bytes;
        }
    }
    return false;
}

static double needNum(const Value& v, const string& where, int line) {
    if (v.type != VT::Num)
        throw WenError("运行时错误", line, 0,
                       "「" + where + "」需要数字，收到" + typeName(v));
    return v.num;
}

static string needStr(const Value& v, const string& where, int line) {
    if (v.type != VT::Str)
        throw WenError("运行时错误", line, 0,
                       "「" + where + "」需要文本，收到" + typeName(v));
    return v.str;
}

static shared_ptr<List> needList(const Value& v, const string& where, int line) {
    if (v.type != VT::List || !v.list)
        throw WenError("运行时错误", line, 0,
                       "「" + where + "」需要列表，收到" + typeName(v));
    return v.list;
}

static shared_ptr<vector<unsigned char>> needBytes(const Value& v, const string& where, int line) {
    if (v.type != VT::Bytes || !v.bytes)
        throw WenError("运行时错误", line, 0,
                       "「" + where + "」需要缓冲，收到" + typeName(v));
    return v.bytes;
}

static bool isPointAt(const Value& p, double x, double y) {
    if (p.type != VT::List || !p.list || p.list->items.size() != 2) return false;
    const auto& a = p.list->items[0];
    const auto& b = p.list->items[1];
    return a.type == VT::Num && b.type == VT::Num && a.num == x && b.num == y;
}

static shared_ptr<List> needSet(const Value& v, const string& where, int line) {
    if (v.type != VT::Set || !v.list)
        throw WenError("运行时错误", line, 0,
                       "「" + where + "」需要集合，收到" + typeName(v));
    return v.list;
}

static shared_ptr<Dict> needDict(const Value& v, const string& where, int line) {
    if (v.type != VT::Dict || !v.dict)
        throw WenError("运行时错误", line, 0,
                       "「" + where + "」需要字典，收到" + typeName(v));
    return v.dict;
}

static long long needIndex(const Value& v, const string& where, int line) {
    double d = needNum(v, where, line);
    if (!std::isfinite(d) || std::floor(d) != d)
        throw WenError("运行时错误", line, 0, where + "必须是整数");
    return (long long)d;
}

static bool parseNumberText(const string& s, double& out) {
    try {
        size_t pos = 0;
        out = std::stod(s, &pos);
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

static vector<string> splitLines(const string& s) {
    vector<string> lines;
    std::istringstream in(s);
    string cur;
    while (std::getline(in, cur)) {
        if (!cur.empty() && cur.back() == '\r') cur.pop_back();
        lines.push_back(cur);
    }
    return lines;
}

struct WenSuffixInfo {
    const char* use;
    const char* ext;
};
static const WenSuffixInfo WEN_SUFFIXES[] = {
    {"脚本", ".脚本"}, {"程序", ".程序"}, {"库", ".库"}, {"用例", ".用例"},
    {"场景", ".场景"}, {"模型", ".模型"}, {"矢量图", ".矢量图"},
    {"数据", ".数据"}, {"表格", ".表格"}, {"日志", ".日志"},
    {"配置", ".配置"}, {"缓冲", ".缓冲"}, {"网页", ".网页"},
    {"着色器", ".着色器"}};
static const char* WEN_SOURCE_SUFFIXES[] = {".脚本", ".程序", ".wen"};
static string resolveWenPath(const string& path) {
    if (fs::exists(fs::u8path(path))) return path;
    for (const auto& s : WEN_SUFFIXES) {
        string p = path + s.ext;
        if (fs::exists(fs::u8path(p))) return p;
    }
    string p = path + ".wen";
    if (fs::exists(fs::u8path(p))) return p;
    return path;
}

static string readWholeFile(const string& path) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) throw IoError("打不开文件：" + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    string s = ss.str();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
    return s;
}

static void writeWholeFile(const string& path, const string& text, bool append) {
    std::ofstream out(fs::u8path(path),
                      std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) throw IoError("打不开文件：" + path);
    out << text;
    if (!out) throw IoError("写入失败：" + path);
}

#ifdef _WIN32
static std::wstring utf8ToWide(const string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// 把 GBK/GB18030 网页转成 UTF-8，避免中文乱码。
static string codePageToUtf8(const string& s, UINT cp) {
    if (s.empty()) return s;
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return s;
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), n);
    int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
    string out((size_t)m, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), n, out.data(), m, nullptr, nullptr);
    return out;
}

static string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return string();
    int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string out((size_t)m, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), m, nullptr, nullptr);
    return out;
}

static bool looksLikeGbk(const string& body) {
    string head = body.substr(0, std::min<size_t>(body.size(), 2048));
    std::transform(head.begin(), head.end(), head.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return head.find("charset=gbk") != string::npos ||
           head.find("charset=\"gbk") != string::npos ||
           head.find("charset=gb2312") != string::npos ||
           head.find("charset=gb18030") != string::npos;
}
#endif

// 抓取网页正文。Windows 用 WinHTTP（支持 https 与重定向）。
static bool unsafeNetHost(const string& url) {
    auto p = url.find("://");
    if (p == string::npos) return true;
    size_t start = p + 3;
    size_t end = url.find_first_of("/:", start);
    string host = url.substr(start, end == string::npos ? string::npos : end - start);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (host.empty() || host == "localhost" || host == "0.0.0.0" || host == "::1") return true;
    if (host.rfind("127.", 0) == 0 || host.rfind("10.", 0) == 0 ||
        host.rfind("192.168.", 0) == 0 || host.rfind("169.254.", 0) == 0)
        return true;
    if (host.rfind("172.", 0) == 0) {
        int second = std::atoi(host.c_str() + 4);
        if (second >= 16 && second <= 31) return true;
    }
    if (host.size() > 6 && host.substr(host.size() - 6) == ".local") return true;
    return false;
}
static string httpGetText(const string& url, int timeoutMs, int line) {
#ifdef _WIN32
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        throw WenError("运行时错误", line, 0, "网址必须以 http:// 或 https:// 开头");
    if (timeoutMs < 1000) timeoutMs = 1000;
    if (timeoutMs > 120000) timeoutMs = 120000;
    std::wstring wurl = utf8ToWide(url);
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[512] = {0};
    wchar_t urlPath[4096] = {0};
    wchar_t extra[2048] = {0};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 511;
    uc.lpszUrlPath = urlPath;
    uc.dwUrlPathLength = 4095;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        throw WenError("运行时错误", line, 0, "网址解析失败：" + url);
    std::wstring reqPath = urlPath;
    if (reqPath.empty()) reqPath = L"/";
    if (extra[0]) reqPath += extra;

    struct Closer {
        void operator()(HINTERNET h) const {
            if (h) WinHttpCloseHandle(h);
        }
    };
    using WinHttpPtr = std::unique_ptr<void, Closer>;
    WinHttpPtr session(WinHttpOpen(L"wen/0.1 (Windows)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        throw WenError("运行时错误", line, 0,
                       "网络初始化失败：" + std::to_string((long long)GetLastError()));
    WinHttpSetTimeouts(session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    WinHttpPtr conn(WinHttpConnect(session.get(), host, (INTERNET_PORT)uc.nPort, 0));
    if (!conn)
        throw WenError("运行时错误", line, 0, "连接失败：" + url);
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    WinHttpPtr req(WinHttpOpenRequest(conn.get(), L"GET", reqPath.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req)
        throw WenError("运行时错误", line, 0, "创建请求失败：" + url);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        throw WenError("运行时错误", line, 0,
                       "发送请求失败：" + std::to_string((long long)GetLastError()));
    if (!WinHttpReceiveResponse(req.get(), nullptr))
        throw WenError("运行时错误", line, 0,
                       "接收响应失败：" + std::to_string((long long)GetLastError()));
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status >= 400)
        throw WenError("运行时错误", line, 0,
                       "抓取失败：HTTP " + std::to_string((long long)status) + "（" + url + "）");
    string body;
    DWORD avail = 0;
    while (true) {
        if (!WinHttpQueryDataAvailable(req.get(), &avail))
            throw WenError("运行时错误", line, 0,
                           "读取响应失败：" + std::to_string((long long)GetLastError()));
        if (avail == 0) break;
        string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), &chunk[0], avail, &read))
            throw WenError("运行时错误", line, 0,
                           "读取响应失败：" + std::to_string((long long)GetLastError()));
        if (read == 0) break;
        body.append(chunk.data(), read);
    }
    // 中文站点常见 GBK/GB2312：按响应头或页面 meta 转成 UTF-8。
    bool gbk = looksLikeGbk(body);    wchar_t ctype[512] = {0};
    DWORD ctypeSize = sizeof(ctype);
    if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_CONTENT_TYPE,
                            WINHTTP_HEADER_NAME_BY_INDEX, ctype, &ctypeSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        string ct = wideToUtf8(ctype);
        std::transform(ct.begin(), ct.end(), ct.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (ct.find("gbk") != string::npos || ct.find("gb2312") != string::npos ||
            ct.find("gb18030") != string::npos)
            gbk = true;
    }
    if (gbk) body = codePageToUtf8(body, 936);
    return body;
#else
    (void)url;
    (void)timeoutMs;
    throw WenError("运行时错误", line, 0, "网络库目前只支持 Windows（WinHTTP）");
#endif
}

// ============================================================
// 7. 运算
// ============================================================

// ---------- JSON ----------
static string utf8FromCodePoint(unsigned cp) {
    string out;
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
    return out;
}

struct JsonReader {
    const string& s;
    size_t i = 0;
    int line;
    JsonReader(const string& text, int ln) : s(text), line(ln) {}

    [[noreturn]] void fail(const string& msg) const {
        throw WenError("运行时错误", line, 0,
                       "JSON 解析失败：" + msg + "（第 " + std::to_string(i + 1) + " 个字符）");
    }
    void ws() {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    bool eat(const char* lit) {
        size_t n = std::strlen(lit);
        if (s.compare(i, n, lit) == 0) {
            i += n;
            return true;
        }
        return false;
    }
    Value parse() {
        ws();
        Value v = value();
        ws();
        if (i != s.size()) fail("后面还有多余内容");
        return v;
    }
    Value value() {
        ws();
        if (i >= s.size()) fail("内容为空");
        char c = s[i];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return vStr(str());
        if (c == 't') {
            if (!eat("true")) fail("非法字面量");
            return vBool(true);
        }
        if (c == 'f') {
            if (!eat("false")) fail("非法字面量");
            return vBool(false);
        }
        if (c == 'n') {
            if (!eat("null")) fail("非法字面量");
            return vNone();
        }
        return number();
    }
    Value object() {
        ++i;
        auto d = std::make_shared<Dict>();
        ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return vDict(d);
        }
        while (true) {
            ws();
            if (i >= s.size() || s[i] != '"') fail("对象的键必须是字符串");
            string key = str();
            ws();
            if (i >= s.size() || s[i] != ':') fail("键后面缺少冒号");
            ++i;
            d->items[key] = value();
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                return vDict(d);
            }
            fail("对象没有闭合");
        }
    }
    Value array() {
        ++i;
        auto l = std::make_shared<List>();
        ws();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return vList(l);
        }
        while (true) {
            l->items.push_back(value());
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                ++i;
                return vList(l);
            }
            fail("数组没有闭合");
        }
    }
    unsigned hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            if (i >= s.size()) fail("\\u 转义不完整");
            char h = s[i++];
            v <<= 4;
            if (h >= '0' && h <= '9') v += (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') v += (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v += (unsigned)(h - 'A' + 10);
            else fail("\\u 转义不是十六进制");
        }
        return v;
    }
    string str() {
        if (i >= s.size() || s[i] != '"') fail("需要字符串");
        ++i;
        string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) fail("转义不完整");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            i += 2;
                            unsigned lo = hex4();
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    out += utf8FromCodePoint(cp);
                    break;
                }
                default: fail("不认识的转义");
            }
        }
        fail("字符串没有闭合");
    }
    Value number() {
        size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        }
        if (i == start) fail("需要数字");
        try {
            return vNum(std::stod(s.substr(start, i - start)));
        } catch (...) {
            fail("数字格式不对");
        }
    }
};

static string jsonQuote(const string& s) {
    string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    out += "\"";
    return out;
}

static string jsonDump(const Value& v, int line) {
    switch (v.type) {
        case VT::None: return "null";
        case VT::Bool: return v.b ? "true" : "false";
        case VT::Num: {
            if (!std::isfinite(v.num))
                throw WenError("运行时错误", line, 0, "JSON 不能表示无穷或 NaN");
            std::ostringstream os;
            os << std::setprecision(15) << v.num;
            return os.str();
        }
        case VT::Str: return jsonQuote(v.str);
        case VT::List:
        case VT::Set: {
            if (!v.list) return "[]";
            string out = "[";
            for (size_t k = 0; k < v.list->items.size(); ++k) {
                if (k) out += ",";
                out += jsonDump(v.list->items[k], line);
            }
            out += "]";
            return out;
        }
        case VT::Dict: {
            if (!v.dict) return "{}";
            string out = "{";
            bool first = true;
            for (const auto& kv : v.dict->items) {
                if (!first) out += ",";
                first = false;
                out += jsonQuote(kv.first) + ":" + jsonDump(kv.second, line);
            }
            out += "}";
            return out;
        }
        case VT::Bytes: {
            if (!v.bytes) return "[]";
            string out = "[";
            for (size_t k = 0; k < v.bytes->size(); ++k) {
                if (k) out += ",";
                out += std::to_string((int)(*v.bytes)[k]);
            }
            return out + "]";
        }
        default:
            throw WenError("运行时错误", line, 0, "JSON 不支持函数类型");
    }
}

// ---------- HTML ----------
static string lowerAscii(string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static string htmlDecodeEntities(const string& s) {
    static const std::map<string, string> named = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
        {"nbsp", " "}, {"copy", "©"}, {"reg", "®"}, {"trade", "™"},
        {"hellip", "…"}, {"mdash", "—"}, {"ndash", "–"},
        {"ldquo", "“"}, {"rdquo", "”"}, {"lsquo", "‘"}, {"rsquo", "’"},
        {"times", "×"}, {"divide", "÷"}, {"middot", "·"},
        {"deg", "°"}, {"plusmn", "±"}, {"sup2", "²"}, {"sup3", "³"},
        {"frac12", "½"}, {"yen", "¥"}, {"euro", "€"}, {"pound", "£"},
        {"cent", "¢"}, {"sect", "§"}, {"para", "¶"},
        {"laquo", "«"}, {"raquo", "»"},
    };
    string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            out += s[i++];
            continue;
        }
        size_t semi = s.find(';', i + 1);
        if (semi == string::npos || semi - i > 12) {
            out += s[i++];
            continue;
        }
        string body = s.substr(i + 1, semi - i - 1);
        if (!body.empty() && body[0] == '#') {
            unsigned cp = 0;
            bool ok = false;
            if (body.size() > 2 && (body[1] == 'x' || body[1] == 'X')) {
                cp = (unsigned)std::strtoul(body.c_str() + 2, nullptr, 16);
                ok = cp > 0;
            } else if (body.size() > 1) {
                cp = (unsigned)std::strtoul(body.c_str() + 1, nullptr, 10);
                ok = cp > 0;
            }
            if (ok) {
                out += utf8FromCodePoint(cp);
                i = semi + 1;
                continue;
            }
        }
        auto it = named.find(lowerAscii(body));
        if (it != named.end()) {
            out += it->second;
            i = semi + 1;
            continue;
        }
        out += s[i++];
    }
    return out;
}

static bool isVoidTag(const string& name) {
    static const std::set<string> voids = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"};
    return voids.count(name) > 0;
}

// 找出所有 <name ...> … </name> 的内部 HTML。
static vector<string> htmlTagContents(const string& html, const string& rawName) {
    string name = lowerAscii(rawName);
    vector<string> out;
    size_t pos = 0;
    while (pos < html.size()) {
        size_t lt = html.find('<', pos);
        if (lt == string::npos) break;
        if (lt + 1 < html.size() && html[lt + 1] == '/') {
            pos = lt + 2;
            continue;
        }
        size_t i = lt + 1;
        string tag;
        while (i < html.size() &&
               (std::isalnum((unsigned char)html[i]) || html[i] == '-' || html[i] == ':'))
            tag += (char)std::tolower((unsigned char)html[i++]);
        if (tag != name) {
            pos = lt + 1;
            continue;
        }
        size_t gt = html.find('>', i);
        if (gt == string::npos) break;
        bool selfClose = gt > lt && html[gt - 1] == '/';
        if (selfClose || isVoidTag(name)) {
            out.push_back("");
            pos = gt + 1;
            continue;
        }
        size_t contentStart = gt + 1;
        int depth = 1;
        size_t p = contentStart;
        size_t closePos = string::npos;
        while (p < html.size()) {
            size_t next = html.find('<', p);
            if (next == string::npos) break;
            if (next + 1 < html.size() && html[next + 1] == '/') {
                size_t j = next + 2;
                string ctag;
                while (j < html.size() &&
                       (std::isalnum((unsigned char)html[j]) || html[j] == '-' || html[j] == ':'))
                    ctag += (char)std::tolower((unsigned char)html[j++]);
                if (ctag == name) {
                    --depth;
                    if (depth == 0) {
                        closePos = next;
                        break;
                    }
                }
                p = next + 2;
            } else {
                size_t j = next + 1;
                string otag;
                while (j < html.size() &&
                       (std::isalnum((unsigned char)html[j]) || html[j] == '-' || html[j] == ':'))
                    otag += (char)std::tolower((unsigned char)html[j++]);
                if (otag == name) {
                    size_t g = html.find('>', j);
                    if (g == string::npos) break;
                    bool sc = g > next && html[g - 1] == '/';
                    if (!sc) ++depth;
                    p = g + 1;
                } else {
                    p = next + 1;
                }
            }
        }
        if (closePos == string::npos) break;
        out.push_back(html.substr(contentStart, closePos - contentStart));
        // 继续扫描内部，嵌套的同名标签也会被取到。
        pos = contentStart;
    }
    return out;
}

// 取某个标签的属性值，例如 取属性(网页, "a", "href")。
static vector<string> htmlAttributeValues(const string& html, const string& rawTag,
                                          const string& rawAttr) {
    string tag = lowerAscii(rawTag);
    string attr = lowerAscii(rawAttr);
    vector<string> out;
    size_t pos = 0;
    while (pos < html.size()) {
        size_t lt = html.find('<', pos);
        if (lt == string::npos) break;
        if (lt + 1 < html.size() && html[lt + 1] == '/') {
            pos = lt + 2;
            continue;
        }
        size_t i = lt + 1;
        string name;
        while (i < html.size() &&
               (std::isalnum((unsigned char)html[i]) || html[i] == '-' || html[i] == ':'))
            name += (char)std::tolower((unsigned char)html[i++]);
        size_t gt = html.find('>', i);
        if (gt == string::npos) break;
        if (name != tag) {
            pos = lt + 1;
            continue;
        }
        string tagText = html.substr(i, gt - i);
        string lower = lowerAscii(tagText);
        size_t p = 0;
        while ((p = lower.find(attr, p)) != string::npos) {
            bool leftOk = (p == 0) || !std::isalnum((unsigned char)lower[p - 1]) &&
                                          lower[p - 1] != '-' && lower[p - 1] != '_';
            size_t after = p + attr.size();
            bool rightOk = (after >= lower.size()) ||
                           !std::isalnum((unsigned char)lower[after]) &&
                               lower[after] != '-' && lower[after] != '_';
            if (!leftOk || !rightOk) {
                p = after;
                continue;
            }
            size_t q = after;
            while (q < lower.size() && std::isspace((unsigned char)lower[q])) ++q;
            if (q >= lower.size() || lower[q] != '=') {
                p = after;
                continue;
            }
            ++q;
            while (q < lower.size() && std::isspace((unsigned char)lower[q])) ++q;
            string val;
            if (q < tagText.size() && (tagText[q] == '"' || tagText[q] == '\'')) {
                char quote = tagText[q++];
                size_t e = tagText.find(quote, q);
                if (e == string::npos) e = tagText.size();
                val = tagText.substr(q, e - q);
                q = (e < tagText.size()) ? e + 1 : e;
            } else {
                size_t e = q;
                while (e < tagText.size() && !std::isspace((unsigned char)tagText[e]) &&
                       tagText[e] != '>')
                    ++e;
                val = tagText.substr(q, e - q);
                q = e;
            }
            out.push_back(htmlDecodeEntities(val));
            p = q;
        }
        pos = gt + 1;
    }
    return out;
}

static string htmlToText(const string& html) {
    string raw;
    string lowerHtml = lowerAscii(html);
    for (size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            if (html.compare(i, 4, "<!--") == 0) {
                size_t e = html.find("-->", i + 4);
                i = (e == string::npos) ? html.size() : e + 3;
                continue;
            }
            // script/style 里的 JS/CSS 不算正文。
            if (lowerHtml.compare(i, 7, "<script") == 0 ||
                lowerHtml.compare(i, 6, "<style") == 0) {
                const char* endTag = (lowerHtml.compare(i, 7, "<script") == 0)
                                         ? "</script" : "</style";
                size_t e = lowerHtml.find(endTag, i + 1);
                if (e == string::npos) break;
                size_t g = html.find('>', e);
                i = (g == string::npos) ? html.size() : g + 1;
                raw += ' ';
                continue;
            }
            size_t e = html.find('>', i);
            if (e == string::npos) break;
            raw += ' ';
            i = e + 1;
            continue;
        }
        if (html[i] == '&') {
            size_t e = html.find(';', i);
            if (e != string::npos && e - i <= 12) {
                raw += htmlDecodeEntities(html.substr(i, e - i + 1));
                i = e + 1;
                continue;
            }
        }
        raw += html[i++];
    }
    string out;
    bool space = false;
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += (char)c;
    }
    return out;
}

static string trimText(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

static string htmlTitleText(const string& html) {
    auto titles = htmlTagContents(html, "title");
    if (titles.empty()) return string();
    return trimText(htmlToText(titles[0]));
}

// ---------- 运算 ----------

static Value applyBinary(const string& op, const Value& a, const Value& b, int line) {
    if (op == "+") {
        if (a.type == VT::Num && b.type == VT::Num) return vNum(a.num + b.num);
        if (a.type == VT::Str && b.type == VT::Str) return vStr(a.str + b.str);
        if (a.type == VT::List && b.type == VT::List) {
            auto l = std::make_shared<List>();
            if (a.list) l->items = a.list->items;
            if (b.list) l->items.insert(l->items.end(), b.list->items.begin(), b.list->items.end());
            return vList(l);
        }
        throw WenError("运行时错误", line, 0,
                       "「+」不支持" + typeName(a) + "和" + typeName(b));
    }
    if (op == "==" || op == "=") return vBool(valueEquals(a, b));
    if (op == "!=") return vBool(!valueEquals(a, b));
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (a.type == VT::Num && b.type == VT::Num) {
            if (op == "<") return vBool(a.num < b.num);
            if (op == "<=") return vBool(a.num <= b.num);
            if (op == ">") return vBool(a.num > b.num);
            return vBool(a.num >= b.num);
        }
        if (a.type == VT::Str && b.type == VT::Str) {
            if (op == "<") return vBool(a.str < b.str);
            if (op == "<=") return vBool(a.str <= b.str);
            if (op == ">") return vBool(a.str > b.str);
            return vBool(a.str >= b.str);
        }
        throw WenError("运行时错误", line, 0,
                       "「" + op + "」不能比较" + typeName(a) + "和" + typeName(b));
    }
    if (op == "在") {
        if (a.type == VT::Str && b.type == VT::Str)
            return vBool(b.str.find(a.str) != string::npos);
        if ((b.type == VT::List || b.type == VT::Set) && b.list) {
            for (const auto& item : b.list->items)
                if (valueEquals(a, item)) return vBool(true);
            return vBool(false);
        }
        if (b.type == VT::Dict && b.dict) {
            if (a.type != VT::Str)
                throw WenError("运行时错误", line, 0, "字典的键必须是文本");
            return vBool(b.dict->items.count(a.str) > 0);
        }
        throw WenError("运行时错误", line, 0,
                       "「在…中」的右边不能是" + typeName(b));
    }

    double x = needNum(a, op, line);
    double y = needNum(b, op, line);
    if (op == "-") return vNum(x - y);
    if (op == "*") return vNum(x * y);
    if (op == "/") {
        if (y == 0) throw WenError("运行时错误", line, 0, "除数不能为 0");
        return vNum(x / y);
    }
    if (op == "//") {
        if (y == 0) throw WenError("运行时错误", line, 0, "除数不能为 0");
        return vNum(std::floor(x / y));
    }
    if (op == "%") {
        if (y == 0) throw WenError("运行时错误", line, 0, "除数不能为 0");
        return vNum(x - std::floor(x / y) * y);
    }
    if (op == "**") {
        double r = std::pow(x, y);
        if (std::isnan(r))
            throw WenError("运行时错误", line, 0,
                           "这个幂运算的结果不是实数（负数不能开非整数次方）");
        return vNum(r);
    }
    throw WenError("运行时错误", line, 0, "未知运算符：" + op);
}

// ============================================================
// 8. 解释器
// ============================================================

// 每个函数属于哪个库；没写「依赖」就用，会报错提醒。
static const std::map<string, string>& functionModules() {
    static const std::map<string, string> m = {
        // 路径 / 日志 / 哈希
        {"路径拼接", "路径库"}, {"路径目录", "路径库"}, {"路径文件名", "路径库"},
        {"路径扩展名", "路径库"}, {"路径绝对", "路径库"}, {"日志时间", "日志库"},
        {"日志", "日志库"}, {"写日志", "日志库"}, {"清空日志", "日志库"},
        {"哈希", "哈希库"}, {"文件哈希", "哈希库"}, {"校验和", "哈希库"},

        // 时间 / 编码 / 正则
        {"现在", "时间库"}, {"时间戳", "时间库"}, {"毫秒时间", "时间库"},
        {"格式化时间", "时间库"}, {"解析时间", "时间库"}, {"休眠", "时间库"},
        {"Base64编码", "编码库"}, {"Base64解码", "编码库"},
        {"URL编码", "编码库"}, {"URL解码", "编码库"},
        {"正则匹配", "正则库"}, {"正则查找", "正则库"},
        {"正则替换", "正则库"}, {"正则分割", "正则库"},

        // 数学库
        {"平方根", "数学库"}, {"绝对值", "数学库"}, {"取整", "数学库"},
        {"向下取整", "数学库"}, {"向上取整", "数学库"}, {"指数", "数学库"},
        {"正弦", "数学库"}, {"余弦", "数学库"}, {"正切", "数学库"},
        {"双曲正弦", "数学库"}, {"双曲余弦", "数学库"}, {"双曲正切", "数学库"},
        {"角度转弧度", "数学库"}, {"弧度转角度", "数学库"}, {"幂", "数学库"},
        {"对数", "数学库"}, {"对数10", "数学库"},
        {"反正弦", "数学库"}, {"反余弦", "数学库"}, {"反正切", "数学库"},
        {"反正切2", "数学库"}, {"符号", "数学库"}, {"随机", "数学库"},
        {"随机整数", "数学库"}, {"最大", "数学库"}, {"最小", "数学库"},
        {"求和", "数学库"}, {"解二次方程", "数学库"},
        {"阶乘", "数学库"}, {"最大公约数", "数学库"}, {"最小公倍数", "数学库"},
        {"是否素数", "数学库"}, {"质因数分解", "数学库"},
        {"平均数", "数学库"}, {"中位数", "数学库"},
        {"方差", "数学库"}, {"样本方差", "数学库"}, {"标准差", "数学库"}, {"样本标准差", "数学库"},
        {"组合数", "数学库"}, {"排列数", "数学库"}, {"矩阵乘", "数学库"},
        {"二次函数", "数学库"}, {"多项式求值", "数学库"},
        {"二元二次求值", "数学库"}, {"解二元一次方程组", "数学库"},
        {"解线性方程组", "数学库"},

        // 图形库
        {"圆面积", "图形库"}, {"圆周长", "图形库"}, {"球体积", "图形库"},
        {"球表面积", "图形库"}, {"圆柱体积", "图形库"}, {"圆锥体积", "图形库"},
        {"长方形面积", "图形库"}, {"三角形面积", "图形库"}, {"梯形面积", "图形库"},
        {"长方体体积", "图形库"}, {"勾股斜边", "图形库"}, {"两点距离", "图形库"},
        {"扇形面积", "图形库"}, {"圆环面积", "图形库"},
        {"圆柱表面积", "图形库"}, {"圆柱侧面积", "图形库"},
        {"圆锥表面积", "图形库"}, {"圆锥侧面积", "图形库"},
        {"长方体表面积", "图形库"}, {"正方体表面积", "图形库"},
        {"面积", "图形库"}, {"表面积", "图形库"}, {"侧面积", "图形库"},

        // 概率库
        {"概率", "概率库"}, {"条件概率", "概率库"}, {"独立概率", "概率库"},
        {"贝叶斯", "概率库"}, {"期望", "概率库"}, {"二项概率", "概率库"},
        {"泊松概率", "概率库"}, {"几何概率", "概率库"}, {"超几何概率", "概率库"},
        {"正态密度", "概率库"}, {"正态累积", "概率库"}, {"均匀概率", "概率库"},
        {"指数概率", "概率库"}, {"归一化", "概率库"}, {"累积概率", "概率库"},
        {"赔率", "概率库"}, {"按权重分配", "概率库"}, {"加权选择", "概率库"},
        {"随机抽样", "概率库"}, {"洗牌", "概率库"},

        // 渲染库
        {"新建面", "渲染库"}, {"添加场景", "渲染库"}, {"新建三维面", "渲染库"}, {"新建立方体", "渲染库"},
        {"新建球", "渲染库"}, {"渲染三维", "渲染库"}, {"导出OBJ", "渲染库"}, {"面面积", "渲染库"}, {"移动面", "渲染库"},
        {"旋转面", "渲染库"}, {"缩放面", "渲染库"}, {"涂鸦", "渲染库"}, {"渐变", "渲染库"},
        {"体积光2D", "渲染库"}, {"体积光3D", "渲染库"},
        {"场景", "渲染库"}, {"清空场景", "渲染库"}, {"导出场景", "渲染库"},
        {"导出OpenGL", "渲染库"}, {"导出WebGPU", "渲染库"}, {"导出Vulkan", "渲染库"},

        // 加速库
        {"开启加速", "跃迁库"}, {"关闭加速", "跃迁库"}, {"清空缓存", "跃迁库"},
        {"缓存大小", "跃迁库"}, {"加速命中", "跃迁库"},

        // 管理员库
        {"申请管理员", "管理员库"}, {"是否管理员", "管理员库"},
        {"退出管理员", "管理员库"}, {"危险模式", "管理员库"},

        // 翻译器
        {"翻译支持", "兼容库"}, {"翻译目标", "兼容库"}, {"翻译检查", "兼容库"},
        {"兼容支持", "兼容库"}, {"兼容翻译", "兼容库"}, {"兼容运行", "兼容库"},
        {"兼容语言", "兼容库"}, {"兼容解析", "兼容库"},
        {"翻译", "兼容库"}, {"执行字节码", "兼容库"},
        {"反汇编", "兼容库"}, {"反汇编文本", "兼容库"},
        {"二进制头", "兼容库"}, {"二进制字符串", "兼容库"}, {"二进制反汇编", "兼容库"},
        {"反二进制", "兼容库"}, {"暴力还原", "兼容库"},

        // CPU / GPU
        {"CPU核心数", "CPU库"}, {"CPU并行求和", "CPU库"}, {"CPU并行最大", "CPU库"},
        {"CPU并行最小", "CPU库"},

        // 内存库
        {"内存存", "内存库"}, {"内存取", "内存库"}, {"内存有", "内存库"},
        {"内存删", "内存库"}, {"内存清空", "内存库"}, {"内存大小", "内存库"},
        {"内存键", "内存库"}, {"内存临时存", "内存库"}, {"内存临时取", "内存库"},
        {"内存清临时", "内存库"}, {"内存命中", "内存库"}, {"开启内存", "内存库"},
        {"关闭内存", "内存库"}, {"内存缓存", "内存库"},
        {"开启内存压缩", "内存库"}, {"关闭内存压缩", "内存库"},
        {"压缩内存", "内存库"}, {"解压内存", "内存库"}, {"压缩全部内存", "内存库"},
        {"内存压缩大小", "内存库"}, {"内存复用率", "内存库"},
        {"牵引", "内存库"}, {"牵引范围", "内存库"}, {"牵引名单", "内存库"},
        {"牵引清空", "内存库"}, {"压缩牵引", "内存库"},
        {"内存压缩率", "内存库"}, {"内存字典", "内存库"}, {"内存对半切", "内存库"},
        {"内存清场", "内存库"}, {"内存压缩阈值", "内存库"}, {"压缩", "内存库"}, {"压缩率", "内存库"}, {"清空", "内存库"},

        // 外派库
        {"外派支持", "兼容库"}, {"外派目标", "兼容库"}, {"外派检查", "兼容库"},
        {"外派翻译", "兼容库"},

        // 跃迁库
        {"跃迁查", "跃迁库"}, {"跃迁记", "跃迁库"}, {"跃迁清空", "跃迁库"},
        {"跃迁调用", "跃迁库"}, {"开启跃迁", "跃迁库"}, {"关闭跃迁", "跃迁库"},
        {"跃迁大小", "跃迁库"}, {"跃迁命中", "跃迁库"}, {"跃迁未命中", "跃迁库"},
        {"跃迁复用率", "跃迁库"}, {"跃迁容量", "跃迁库"}, {"跃迁快照", "跃迁库"},
        {"跃迁恢复", "跃迁库"}, {"跃迁导出", "跃迁库"}, {"跃迁导入", "跃迁库"},

        // 物理库
        {"速度", "物理库"}, {"加速度", "物理库"}, {"力", "物理库"},
        {"功", "物理库"}, {"功率", "物理库"}, {"动能", "物理库"},
        {"势能", "物理库"}, {"动量", "物理库"}, {"密度", "物理库"},
        {"压强", "物理库"}, {"浮力", "物理库"}, {"电压", "物理库"},
        {"电流", "物理库"}, {"电阻", "物理库"}, {"波速", "物理库"},
        {"周期", "物理库"}, {"频率", "物理库"},
        {"自由落体时间", "物理库"}, {"自由落体速度", "物理库"},
        {"向心加速度", "物理库"}, {"万有引力", "物理库"},
        {"摄氏转华氏", "物理库"}, {"华氏转摄氏", "物理库"},
        {"摄氏转开尔文", "物理库"}, {"开尔文转摄氏", "物理库"},
        {"体积", "物理库"}, {"重力", "物理库"}, {"滑动摩擦力", "物理库"},
        {"弹力", "物理库"}, {"向心力", "物理库"}, {"角速度", "物理库"},
        {"线速度", "物理库"}, {"单摆周期", "物理库"}, {"弹性势能", "物理库"},
        {"机械效率", "物理库"}, {"杠杆阻力", "物理库"}, {"液体压强", "物理库"},
        {"流量", "物理库"}, {"热量", "物理库"}, {"热值放热", "物理库"},
        {"热效率", "物理库"}, {"热膨胀伸长", "物理库"},
        {"理想气体压强", "物理库"}, {"理想气体体积", "物理库"},
        {"电功率", "物理库"}, {"电能", "物理库"}, {"焦耳热", "物理库"},
        {"串联电阻", "物理库"}, {"并联电阻", "物理库"},
        {"电场力", "物理库"}, {"电势能", "物理库"},
        {"折射率", "物理库"}, {"折射定律", "物理库"}, {"临界角", "物理库"},
        {"透镜像距", "物理库"}, {"透镜放大率", "物理库"},
        {"光子能量", "物理库"}, {"质能", "物理库"}, {"德布罗意波长", "物理库"},
        {"第一宇宙速度", "物理库"}, {"轨道周期", "物理库"},

        // 函数库
        {"映射", "函数库"}, {"过滤", "函数库"}, {"折叠", "函数库"},
        {"每个", "函数库"}, {"复合", "函数库"}, {"迭代", "函数库"},
        {"斜率", "函数库"}, {"定积分", "函数库"},
        {"任意", "函数库"}, {"全部", "函数库"}, {"找到", "函数库"},
        {"计数", "函数库"}, {"排序依据", "函数库"}, {"去重", "函数库"},
        {"展平", "函数库"},

        // 可视化库
        {"折线图", "可视化库"}, {"柱状图", "可视化库"},
        {"散点图", "可视化库"}, {"饼图", "可视化库"},

        // 文件库
        {"读文件", "文件库"}, {"写文件", "文件库"}, {"追加文件", "文件库"},
        {"文件大小", "文件库"}, {"列目录", "文件库"}, {"复制文件", "文件库"},
        {"重命名", "文件库"}, {"创建目录", "文件库"},
        {"文件存在", "文件库"}, {"删除文件", "文件库"},

        // 输入库
        {"读整数", "输入库"}, {"读小数", "输入库"}, {"确认", "输入库"},
        {"接收原始输入", "输入库"}, {"取输入", "输入库"},
        {"预览输入", "输入库"}, {"清空输入", "输入库"},
        {"读键", "输入库"}, {"清屏", "输入库"},

        // 网络库
        {"抓取", "网络库"}, {"下载", "网络库"}, {"抓取JSON", "网络库"},
        {"安全抓取", "网络库"}, {"限速抓取", "网络库"}, {"乱码解析", "网络库"},
        {"二进制破坏", "网络库"}, {"二进制恢复", "网络库"},

        // JSON 库
        {"解析JSON", "数据格式库"}, {"生成JSON", "数据格式库"},
        {"解析CSV", "数据格式库"}, {"生成CSV", "数据格式库"},

        // HTML 库
        {"去标签", "网页库"}, {"解码实体", "网页库"}, {"取标签", "网页库"},
        {"取属性", "网页库"}, {"取链接", "网页库"}, {"取标题", "网页库"},

        // 集合库
        {"新建集合", "集合库"}, {"集合添加", "集合库"}, {"集合删除", "集合库"},
        {"集合存在", "集合库"}, {"并集", "集合库"}, {"交集", "集合库"},
        {"差集", "集合库"}, {"子集", "集合库"}, {"对称差", "集合库"},

        // 点阵库
        {"新建点阵", "点阵库"}, {"点阵添加", "点阵库"},
        {"点阵移除", "点阵库"}, {"点阵包含", "点阵库"},
        {"点阵数量", "点阵库"}, {"点阵平移", "点阵库"}, {"点阵缩放", "点阵库"},

        // 频道库
        {"创建频道", "频道库"}, {"发送消息", "频道库"}, {"读取消息", "频道库"},
        {"窥视消息", "频道库"}, {"频道长度", "频道库"}, {"清空频道", "频道库"},
        {"频道全部", "频道库"},

        // 注入库
        {"注入输入事件", "注入库"}, {"注入变量", "注入库"},
        {"读取变量", "注入库"}, {"变量名单", "注入库"}, {"执行文本", "注入库"},
        {"变量存在", "注入库"}, {"删除变量", "注入库"},
    };
    return m;
}

struct ReturnSignal { Value value; int line; };
struct BreakSignal { int line; };
struct ContinueSignal { int line; };

class Interpreter {
public:
    shared_ptr<Scope> globals;
    std::mt19937 rng;
    string filename;
    vector<string> sourceLines;
    bool lastWasExpr = false;
    Value lastValue;
    std::set<string> loadedModules;
    std::map<string, string> symbolOwner;  // 名字 -> 库/用户，防止静默覆盖
    bool callStats = false;                // 调用统计：只在开启时计数
    bool adminGranted = false;             // 管理员权限
    bool dangerousMode = false;            // 危险模式：允许二进制破坏
    std::map<string, long long> callCounts;
    std::map<string, std::map<string, Value>> libraryFunctions;  // 库名 -> 函数表
    std::vector<Value> renderScene;  // 渲染场景：面/涂鸦/渐变/体积光，供各后端使用
    bool accelEnabled = false;       // 加速库：纯函数结果缓存开关
    std::map<string, Value> accelCache;
    long long accelHits = 0;
    std::map<string, Value> leapCache;  // 跃迁库：同键直接跳过计算
    bool leapAuto = false;               // 跃迁库：自动缓存用户函数
    long long leapHits = 0;
    long long leapMisses = 0;
    size_t leapLimit = 200000;
    std::map<string, Value> memoryStore;  // 内存库：常驻
    std::map<string, Value> memoryTemp;   // 内存库：临时
    std::map<string, string> memoryCompressed;  // 内存库：压缩区
    std::map<string, size_t> memoryOrigSize;    // 压缩前字节数
    std::vector<string> memoryDict;             // 符号字典
    std::map<string, int> memoryDictIndex;
    bool memoryAuto = false;             // 内存库：用户函数自动缓存
    bool memoryCompress = false;         // 内存库：新缓存直接压缩
    double memoryMinSavings = 0.0;       // 压缩阈值：至少省这么多才压
    std::set<string> memoryPullKeys;     // 牵引：精确键
    std::vector<string> memoryPullPrefixes;  // 牵引：前缀/范围
    bool memoryPullAll = false;          // 牵引：全部
    long long memoryHits = 0;
    long long memoryMisses = 0;
    std::map<string, std::deque<Value>> channels;
    std::deque<string> inputBuffer;
    bool inLibrary_ = false;
    std::set<string> moduleExports;

    Interpreter();

    void 加载模块(const string& name, int line);
    void 加载外部库(const string& name, int line);
    bool 是内置库(const string& name) const;
    void registerBuiltin(const string& name, int minArgs, int maxArgs,
                         std::function<Value(Interpreter&, vector<Value>&, int)> fn);

    vector<StmtPtr> parseSource(const string& src);
    void scanPull(const string& src);
    bool isPulled(const string& key) const;
    string leapKey(const Value& key, int line);
    void buildMemoryDict(const string& extra = "");
    string compressMemoryText(const string& text);
    string decompressMemoryText(const string& text, int line);
    void run(const string& src, const string& name);
    void execProgram(const vector<StmtPtr>& program);
    void execProgram(const vector<StmtPtr>& program, shared_ptr<Scope> scope);
    void exec(const StmtPtr& s, shared_ptr<Scope> scope);
    Value eval(const ExprPtr& e, shared_ptr<Scope> scope);
    Value callValue(const Value& callee, vector<Value>& args, int line);
    void assignTarget(const ExprPtr& target, const Value& val, shared_ptr<Scope> scope);
    Value getIndex(const Value& obj, const Value& idx, int line);
    void setIndex(Value& obj, const Value& idx, const Value& val, int line);

    [[noreturn]] void rtError(int line, const string& msg) {
        throw WenError("运行时错误", line, 0, msg);
    }

    // 名字不存在：如果它属于某个还没「依赖」的库，给出依赖提示。
    [[noreturn]] void undefinedName(const string& name, int line) {
        for (const auto& kv : libraryFunctions) {
            if (kv.second.count(name))
                throw WenError("运行时错误", line, 0,
                               "「" + name + "」属于「" + kv.first +
                                   "」，请先在文件开头写「依赖 " + kv.first + "」");
        }
        static const std::map<string, string> constLib = {
            {"圆周率", "数学库"}, {"π", "数学库"}, {"自然常数", "数学库"},
            {"重力加速度", "物理库"}, {"光速", "物理库"},
            {"万有引力常数", "物理库"}, {"声速", "物理库"}};
        auto ci = constLib.find(name);
        if (ci != constLib.end())
            throw WenError("运行时错误", line, 0,
                           "「" + name + "」属于「" + ci->second +
                               "」，请先在文件开头写「依赖 " + ci->second + "」");
        throw WenError("运行时错误", line, 0, "变量「" + name + "」还没有值");
    }
};

// ============================================================
// 9. 解释器实现
// ============================================================

void Interpreter::registerBuiltin(
    const string& name, int minArgs, int maxArgs,
    std::function<Value(Interpreter&, vector<Value>&, int)> fn) {
    auto b = std::make_shared<Builtin>();
    b->name = name;
    b->minArgs = minArgs;
    b->maxArgs = maxArgs;
    b->fn = std::move(fn);
    globals->vars[name] = vFunc(b);
}

vector<StmtPtr> Interpreter::parseSource(const string& src) {
    Lexer lex(src);
    auto toks = lex.scan();
    Parser p(std::move(toks));
    return p.parseProgram();
}

void Interpreter::scanPull(const string& src) {
    std::istringstream in(src);
    string line;
    while (std::getline(in, line)) {
        string t = trimText(line);
        if (t.empty() || t[0] != '#') continue;
        t = trimText(t.substr(1));
        if (t.rfind("牵引", 0) != 0) continue;
        t = trimText(t.substr(6));
        if (!t.empty() && (t[0] == ':' || startsWith(t, 0, "：")))
            t = trimText(t.substr(t[0] == ':' ? 1 : 3));
        std::istringstream parts(t);
        string item;
        while (std::getline(parts, item, ',')) {
            item = trimText(item);
            if (item.empty()) continue;
            if (item == "全部" || item == "*") {
                memoryPullAll = true;
            } else if (!item.empty() && item.back() == '*') {
                memoryPullPrefixes.push_back(item.substr(0, item.size() - 1));
            } else {
                memoryPullKeys.insert(item);
            }
        }
    }
}

bool Interpreter::isPulled(const string& key) const {
    if (memoryPullAll) return true;
    if (memoryPullKeys.count(key)) return true;
    for (const auto& p : memoryPullPrefixes)
        if (startsWith(key, 0, p)) return true;
    return false;
}

static vector<string> memoryTokens(const string& s) {
    vector<string> out;
    string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    };
    for (unsigned char c : s) {
        if (c == '{' || c == '}' || c == '[' || c == ']' || c == '"' || c == ':' ||
            c == ',' || c == ' ' || c == '\n' || c == '\t' || c == '\r')
            flush();
        else
            cur += (char)c;
    }
    flush();
    return out;
}

void Interpreter::buildMemoryDict(const string& extra) {
    vector<std::pair<string, string>> compRaws;
    for (const auto& kv : memoryCompressed)
        compRaws.push_back({kv.first, decompressMemoryText(kv.second, 0)});
    memoryDict.clear();
    memoryDictIndex.clear();
    std::map<string, int> freq;
    auto scanText = [&](const string& text) {
        for (const auto& t : memoryTokens(text))
            if (t.size() >= 3) ++freq[t];
    };
    for (const auto& kv : memoryStore) scanText(jsonDump(kv.second, 0));
    for (const auto& kv : memoryTemp) scanText(jsonDump(kv.second, 0));
    for (const auto& kv : compRaws) scanText(kv.second);
    if (!extra.empty()) scanText(extra);
    struct Cand {
        string token;
        long long score;
    };
    vector<Cand> cands;
    for (const auto& kv : freq) {
        if (kv.second < 2) continue;
        long long score = (long long)(kv.first.size() - 2) * kv.second;
        if (score > 0) cands.push_back({kv.first, score});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.token < b.token;
    });
    int id = 1;
    for (const auto& c : cands) {
        if (id > 255) break;
        memoryDict.push_back(c.token);
        memoryDictIndex[c.token] = id++;
    }
    for (const auto& kv : compRaws) {
        memoryOrigSize[kv.first] = kv.second.size();
        memoryCompressed[kv.first] = compressMemoryText(kv.second);
    }
}

string Interpreter::compressMemoryText(const string& text) {
    if (memoryDict.empty() || text.empty()) return text;
    string out;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = text[i];
        bool delim = (c == '{' || c == '}' || c == '[' || c == ']' || c == '"' || c == ':' ||
                      c == ',' || c == ' ' || c == '\n' || c == '\t' || c == '\r');
        if (delim) {
            out += (char)c;
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size()) {
            unsigned char d = text[j];
            if (d == '{' || d == '}' || d == '[' || d == ']' || d == '"' || d == ':' ||
                d == ',' || d == ' ' || d == '\n' || d == '\t' || d == '\r')
                break;
            ++j;
        }
        string tok = text.substr(i, j - i);
        auto it = memoryDictIndex.find(tok);
        if (it != memoryDictIndex.end()) {
            out += (char)1;
            out += (char)it->second;
        } else {
            for (char ch : tok) {
                if ((unsigned char)ch == 1) {
                    out += (char)1;
                    out += (char)0;
                } else {
                    out += ch;
                }
            }
        }
        i = j;
    }
    if (out.size() >= text.size()) return text;
    double saving = 1.0 - (double)out.size() / (double)text.size();
    if (saving < memoryMinSavings) return text;
    if (decompressMemoryText(out, 0) != text) return text;
    return out;
}

static string accelKey(const string& name, const vector<Value>& args);
string Interpreter::leapKey(const Value& key, int line) {
    if (key.type == VT::Str) return key.str;
    vector<Value> one{key};
    return "key|" + accelKey("", one);
}

string Interpreter::decompressMemoryText(const string& text, int line) {
    string out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        if (c != 1) {
            out += (char)c;
            continue;
        }
        if (i + 1 >= text.size())
            throw WenError("运行时错误", line, 0, "压缩数据损坏：缺少符号编号");
        unsigned char id = text[++i];
        if (id == 0) {
            out += (char)1;
        } else if (id <= memoryDict.size()) {
            out += memoryDict[id - 1];
        } else {
            throw WenError("运行时错误", line, 0, "压缩数据损坏：未知符号 " +
                                                   std::to_string((int)id));
        }
    }
    return out;
}

void Interpreter::run(const string& src, const string& name) {
    filename = name;
    sourceLines = splitLines(src);
    scanPull(src);
    try {
        auto program = parseSource(src);
        execProgram(program);
    } catch (IncompleteError& e) {
        throw WenError("语法错误", e.line, 0, e.what());
    }
}

void Interpreter::加载模块(const string& rawName, int line) {
    string name = rawName;
    if (name == "JSON库") name = "数据格式库";
    else if (name == "HTML库") name = "网页库";
    else if (name == "加速库") name = "跃迁库";
    else if (name == "外派库") name = "兼容库";
    else if (name == "翻译器") name = "兼容库";
    if (loadedModules.count(name)) return;
    auto lib = libraryFunctions.find(name);
    if (lib == libraryFunctions.end()) {
        string list;
        for (const auto& kv : libraryFunctions) {
            if (!list.empty()) list += "、";
            list += kv.first;
        }
        throw WenError("运行时错误", line, 0,
                       "未知库「" + name + "」；目前支持：" + list);
    }
    // 核心只负责安装：库自己声明了哪些函数，这里就装哪些。
    for (const auto& kv : lib->second) {
        auto owner = symbolOwner.find(kv.first);
        if (owner != symbolOwner.end() && owner->second != name)
            throw WenError("库冲突", line, 0,
                           "函数「" + kv.first + "」已被「" + owner->second +
                               "」占用；依赖「" + name + "」会覆盖它，请改名或去掉其中一个库");
        globals->vars[kv.first] = kv.second;
        symbolOwner[kv.first] = name;
        if (auto b = std::dynamic_pointer_cast<Builtin>(kv.second.callable)) {
            b->module = name;
            b->moduleKnown = true;
            b->cacheableFn = cacheableBuiltin(kv.first);
        }
    }
    if (name == "数学库") {
        const double pi = std::acos(-1.0);
        globals->vars["圆周率"] = vNum(pi);
        globals->vars["π"] = vNum(pi);
        globals->vars["自然常数"] = vNum(std::exp(1.0));
    } else if (name == "物理库") {
        globals->vars["重力加速度"] = vNum(9.80665);
        globals->vars["光速"] = vNum(299792458.0);
        globals->vars["万有引力常数"] = vNum(6.67430e-11);
        globals->vars["声速"] = vNum(343.0);
        globals->vars["普朗克常数"] = vNum(6.62607015e-34);
        globals->vars["电子电荷"] = vNum(1.602176634e-19);
        globals->vars["阿伏伽德罗常数"] = vNum(6.02214076e23);
        globals->vars["理想气体常数"] = vNum(8.314462618);
        globals->vars["静电力常量"] = vNum(8.9875517923e9);
    }
    loadedModules.insert(name);
}

bool Interpreter::是内置库(const string& rawName) const {
    string name = rawName;
    if (name == "JSON库") name = "数据格式库";
    else if (name == "HTML库") name = "网页库";
    else if (name == "加速库") name = "跃迁库";
    else if (name == "外派库") name = "兼容库";
    else if (name == "翻译器") name = "兼容库";
    return libraryFunctions.count(name) > 0;
}

void Interpreter::加载外部库(const string& name, int line) {
    if (loadedModules.count(name)) return;

    static const char* suffixes[] = {".库", ".wenlib", ".wen"};
    vector<fs::path> dirs;
    std::error_code ec;
    if (!filename.empty() && filename[0] != '<') {
        dirs.push_back(fs::u8path(filename).parent_path());
    }
    dirs.push_back(fs::current_path(ec) / fs::u8path("库"));
    dirs.push_back(fs::current_path(ec));

    vector<fs::path> candidates;
    for (const auto& dir : dirs) {
        // 目录库：库/名字/主.wen
        candidates.push_back(dir / fs::u8path(name) / fs::u8path("主.wen"));
        // 单文件库：库/名字.wen、库/名字.w、库/名字.文 …
        for (const char* suf : suffixes) {
            candidates.push_back(dir / fs::u8path(name + suf));
        }
    }

    fs::path found;
    for (const auto& p : candidates) {
        if (fs::exists(p, ec) && !ec) {
            found = p;
            break;
        }
    }
    if (found.empty()) {
        throw WenError("运行时错误", line, 0,
                       "找不到库「" + name +
                       "」：不是内置库，也没找到 库/" + name +
                       ".脚本/.程序/.wen（库还认 .库/.wenlib，或目录 " + name + "/主.wen）");
    }

    loadedModules.insert(name);
    string oldFile = filename;
    vector<string> oldLines = sourceLines;
    bool oldInLibrary = inLibrary_;
    std::set<string> oldExports = moduleExports;
    inLibrary_ = true;
    moduleExports.clear();
    try {
        string src = readWholeFile(found.u8string());
        filename = found.u8string();
        sourceLines = splitLines(src);
        auto program = parseSource(src);
        auto moduleScope = std::make_shared<Scope>(globals);
        execProgram(program, moduleScope);

        auto installExport = [&](const string& n, const Value& val) {
            auto owner = symbolOwner.find(n);
            if (owner != symbolOwner.end() && owner->second != name)
                throw WenError("库冲突", line, 0,
                               "名字「" + n + "」已被「" + owner->second +
                                   "」占用；库「" + name + "」不能再导出它");
            globals->vars[n] = val;
            symbolOwner[n] = name;
        };
        if (moduleExports.empty()) {
            for (const auto& kv : moduleScope->vars) installExport(kv.first, kv.second);
        } else {
            for (const auto& n : moduleExports) {
                auto it = moduleScope->vars.find(n);
                if (it == moduleScope->vars.end()) {
                    throw WenError("运行时错误", line, 0,
                                   "库「" + name + "」导出的名字「" + n +
                                   "」没有定义");
                }
                installExport(n, it->second);
            }
        }
        filename = oldFile;
        sourceLines = oldLines;
        inLibrary_ = oldInLibrary;
        moduleExports = oldExports;
    } catch (WenError& e) {
        loadedModules.erase(name);
        filename = oldFile;
        sourceLines = oldLines;
        inLibrary_ = oldInLibrary;
        moduleExports = oldExports;
        throw WenError("运行时错误", line, 0,
                       "库「" + name + "」加载失败（第 " +
                       std::to_string(e.line) + " 行）：" + e.what());
    } catch (IncompleteError& e) {
        loadedModules.erase(name);
        filename = oldFile;
        sourceLines = oldLines;
        inLibrary_ = oldInLibrary;
        moduleExports = oldExports;
        throw WenError("语法错误", line, 0,
                       "库「" + name + "」没有写完（第 " +
                       std::to_string(e.line) + " 行）：" + e.what());
    } catch (std::exception& e) {
        loadedModules.erase(name);
        filename = oldFile;
        sourceLines = oldLines;
        inLibrary_ = oldInLibrary;
        moduleExports = oldExports;
        throw WenError("运行时错误", line, 0,
                       "库「" + name + "」加载失败：" + e.what());
    }
}

void Interpreter::execProgram(const vector<StmtPtr>& program) {
    execProgram(program, globals);
}

void Interpreter::execProgram(const vector<StmtPtr>& program, shared_ptr<Scope> scope) {
    try {
        for (const auto& s : program) exec(s, scope);
    } catch (ReturnSignal& r) {
        rtError(r.line, "「返回」只能用在函数里");
    } catch (BreakSignal& b) {
        rtError(b.line, "「中断」只能用在循环里");
    } catch (ContinueSignal& c) {
        rtError(c.line, "「继续」只能用在循环里");
    }
}

void Interpreter::exec(const StmtPtr& s, shared_ptr<Scope> scope) {
    if (s->k != Stmt::K::ExprStmt) lastWasExpr = false;

    switch (s->k) {
        case Stmt::K::Assign: {
            Value v = eval(s->expr, scope);
            assignTarget(s->target, v, scope);
            break;
        }
        case Stmt::K::Print: {
            string out;
            for (size_t i = 0; i < s->args.size(); ++i) {
                if (i) out += " ";
                out += toText(eval(s->args[i], scope));
            }
            std::cout << out << "\n";
            break;
        }
        case Stmt::K::If: {
            bool done = false;
            for (const auto& br : s->branches) {
                if (truthy(eval(br.first, scope))) {
                    for (const auto& st : br.second) exec(st, scope);
                    done = true;
                    break;
                }
            }
            if (!done) {
                for (const auto& st : s->elseBody) exec(st, scope);
            }
            break;
        }
        case Stmt::K::While: {
            while (truthy(eval(s->expr, scope))) {
                try {
                    for (const auto& st : s->body) exec(st, scope);
                } catch (ContinueSignal&) {
                    continue;
                } catch (BreakSignal&) {
                    break;
                }
            }
            break;
        }
        case Stmt::K::For: {
            Value iter = eval(s->expr, scope);
            bool two = !s->name2.empty();
            auto runOne = [&](const Value& k, const Value& v) -> bool {
                scope->vars[s->name] = k;
                if (two) scope->vars[s->name2] = v;
                try {
                    for (const auto& st : s->body) exec(st, scope);
                } catch (ContinueSignal&) {
                    return true;
                } catch (BreakSignal&) {
                    return false;
                }
                return true;
            };
            if ((iter.type == VT::List || iter.type == VT::Set) && iter.list) {
                for (size_t i = 0; i < iter.list->items.size(); ++i) {
                    if (!runOne(two ? vNum((double)i) : iter.list->items[i],
                                two ? iter.list->items[i] : vNone()))
                        break;
                }
            } else if (iter.type == VT::Str) {
                auto chars = utf8Chars(iter.str);
                for (size_t i = 0; i < chars.size(); ++i) {
                    if (!runOne(two ? vNum((double)i) : vStr(chars[i]),
                                two ? vStr(chars[i]) : vNone()))
                        break;
                }
            } else if (iter.type == VT::Dict && iter.dict) {
                for (const auto& kv : iter.dict->items) {
                    if (!runOne(vStr(kv.first), two ? kv.second : vNone())) break;
                }
            } else if (iter.type == VT::Range) {
                long long n = 0;
                for (double x = iter.rstart; iter.rstep > 0 ? x < iter.rstop : x > iter.rstop;
                     x += iter.rstep) {
                    if (!runOne(two ? vNum((double)n) : vNum(x), two ? vNum(x) : vNone()))
                        break;
                    ++n;
                }
            } else {
                rtError(s->line, "「对于」只能遍历列表、文本、字典、集合或范围，收到" +
                                     typeName(iter));
            }
            break;
        }        case Stmt::K::Try: {
            auto runFinally = [&]() {
                for (const auto& st : s->finallyBody) exec(st, scope);
            };
            try {
                for (const auto& st : s->body) exec(st, scope);
            } catch (WenError& e) {
                if (s->name.empty()) {
                    runFinally();
                    throw;
                }
                auto d = std::make_shared<Dict>();
                d->items["类型"] = vStr(e.kind);
                d->items["原因"] = vStr(e.what());
                d->items["行"] = vNum(e.line);
                d->items["列"] = vNum(e.col);
                d->items["文件"] = vStr(filename);
                auto stack = std::make_shared<List>();
                for (const auto& f : e.frames) stack->items.push_back(vStr(f));
                d->items["栈"] = vList(stack);
                scope->vars[s->name] = vDict(d);
                try {
                    for (const auto& st : s->elseBody) exec(st, scope);
                } catch (...) {
                    runFinally();
                    throw;
                }
                runFinally();
                break;
            } catch (...) {
                runFinally();
                throw;
            }
            runFinally();
            break;
        }
        case Stmt::K::Func: {
            auto fn = std::make_shared<Function>();
            fn->name = s->name;
            fn->params = s->params;
            fn->body = s->body;
            fn->closure = scope;
            fn->line = s->line;
            if (!inLibrary_ && (scope == globals || (scope->parent && scope->parent == globals))) {
                auto owner = symbolOwner.find(s->name);
                if (owner != symbolOwner.end() && owner->second != "用户")
                    throw WenError("库冲突", s->line, 0,
                                   "名字「" + s->name + "」已被库「" +
                                       owner->second + "」占用；请改名");
                symbolOwner[s->name] = "用户";
            }
            scope->vars[s->name] = vFunc(fn);
            break;
        }
        case Stmt::K::Return: {
            Value v = s->expr ? eval(s->expr, scope) : vNone();
            throw ReturnSignal{v, s->line};
        }
        case Stmt::K::Break:
            throw BreakSignal{s->line};
        case Stmt::K::Continue:
            throw ContinueSignal{s->line};
        case Stmt::K::Import: {
            for (const auto& n : s->imports) {
                if (是内置库(n)) 加载模块(n, s->line);
                else 加载外部库(n, s->line);
            }
            break;
        }
        case Stmt::K::Export: {
            if (!inLibrary_)
                rtError(s->line, "「导出」只能写在库里");
            for (const auto& n : s->imports) moduleExports.insert(n);
            break;
        }
        case Stmt::K::ExprStmt: {
            lastValue = eval(s->expr, scope);
            lastWasExpr = true;
            break;
        }
    }
}

Value Interpreter::eval(const ExprPtr& e, shared_ptr<Scope> scope) {
    switch (e->k) {
        case Expr::K::Num: return vNum(e->num);
        case Expr::K::Str: return vStr(e->text);
        case Expr::K::Bool: return vBool(e->b);
        case Expr::K::None: return vNone();
        case Expr::K::Var: {
            Value v;
            if (!scope->lookup(e->text, v)) undefinedName(e->text, e->line);
            // 零参数函数/内置：名字后面没写括号，也直接调用。
            // 要把它当值传给别人，请用有参数的函数（至少一个参数）。
            if (v.type == VT::Func && v.callable && v.callable->zeroArgCall()) {
                vector<Value> noArgs;
                return callValue(v, noArgs, e->line);
            }
            return v;
        }
        case Expr::K::Unary: {
            Value a = eval(e->args[0], scope);
            if (e->op == "-") return vNum(-needNum(a, "负号", e->line));
            if (e->op == "+") return vNum(needNum(a, "正号", e->line));
            if (e->op == "非") return vBool(!truthy(a));
            rtError(e->line, "未知一元运算：" + e->op);
        }
        case Expr::K::Binary: {
            if (e->op == "且") {
                Value a = eval(e->args[0], scope);
                if (!truthy(a)) return vBool(false);
                Value b = eval(e->args[1], scope);
                return vBool(truthy(b));
            }
            if (e->op == "或") {
                Value a = eval(e->args[0], scope);
                if (truthy(a)) return vBool(true);
                Value b = eval(e->args[1], scope);
                return vBool(truthy(b));
            }
            Value a = eval(e->args[0], scope);
            Value b = eval(e->args[1], scope);
            return applyBinary(e->op, a, b, e->line);
        }
        case Expr::K::Call: {
            // 指挥("名字", 参数…) 直接派发，省掉“内置指挥 + 目标函数”两层调用。
            if (e->commandCall) {
                Value cmd;
                if (scope->lookup("指挥", cmd) && cmd.type == VT::Func &&
                    std::dynamic_pointer_cast<Builtin>(cmd.callable)) {
                    if (e->args.size() < 2)
                        rtError(e->line, "「指挥」至少需要一个命令名");
                    string name = needStr(eval(e->args[1], scope), "指挥", e->line);
                    Value target;
                    if (!globals->lookup(name, target))
                        rtError(e->line, "指挥层不认识命令「" + name + "」");
                    vector<Value> cargs;
                    cargs.reserve(e->args.size() - 2);
                    for (size_t i = 2; i < e->args.size(); ++i)
                        cargs.push_back(eval(e->args[i], scope));
                    return callValue(target, cargs, e->line);
                }
            }
            // 被调用者要取“原始函数值”，不能再触发零参数自动调用，
            // 否则 零() 会先执行一次再被当成数字调用。
            Value callee;
            if (e->args[0]->k == Expr::K::Var) {
                if (!scope->lookup(e->args[0]->text, callee))
                    undefinedName(e->args[0]->text, e->line);
            } else {
                callee = eval(e->args[0], scope);
            }
            vector<Value> args;
            for (size_t i = 1; i < e->args.size(); ++i)
                args.push_back(eval(e->args[i], scope));
            return callValue(callee, args, e->line);
        }
        case Expr::K::Index: {
            Value obj = eval(e->args[0], scope);
            Value idx = eval(e->args[1], scope);
            return getIndex(obj, idx, e->line);
        }
        case Expr::K::Slice: {
            Value obj = eval(e->args[0], scope);
            long long step = 1;
            if (e->args[3]) step = needIndex(eval(e->args[3], scope), "切片步长", e->line);
            if (step == 0) rtError(e->line, "切片步长不能是 0");
            vector<Value> items;
            bool isText = false;
            if ((obj.type == VT::List || obj.type == VT::Set) && obj.list) {
                items = obj.list->items;
            } else if (obj.type == VT::Str) {
                for (const auto& ch : utf8Chars(obj.str)) items.push_back(vStr(ch));
                isText = true;
            } else {
                rtError(e->line, "切片只能用在列表、集合或文本上，收到" + typeName(obj));
            }
            long long n = (long long)items.size();
            bool hasStart = (bool)e->args[1];
            bool hasStop = (bool)e->args[2];
            long long start = hasStart ? 0 : (step > 0 ? 0 : n - 1);
            long long stop = hasStop ? 0 : (step > 0 ? n : -1);
            if (hasStart) start = needIndex(eval(e->args[1], scope), "切片起点", e->line);
            if (hasStop) stop = needIndex(eval(e->args[2], scope), "切片终点", e->line);
            if (hasStart && start < 0) start += n;
            if (hasStop && stop < 0) stop += n;
            if (step > 0) {
                start = std::max(0LL, std::min(start, n));
                stop = std::max(0LL, std::min(stop, n));
            } else {
                start = std::max(-1LL, std::min(start, n - 1));
                stop = std::max(-1LL, std::min(stop, n - 1));
            }
            auto out = std::make_shared<List>();
            for (long long i = start; step > 0 ? i < stop : i > stop; i += step)
                out->items.push_back(items[(size_t)i]);
            if (isText) {
                string s;
                for (const auto& v : out->items) s += v.str;
                return vStr(s);
            }
            return vList(out);
        }
        case Expr::K::List: {
            auto l = std::make_shared<List>();
            for (const auto& a : e->args) l->items.push_back(eval(a, scope));
            return vList(l);
        }
        case Expr::K::Dict: {
            auto d = std::make_shared<Dict>();
            for (const auto& p : e->pairs) {
                Value k = eval(p.first, scope);
                if (k.type != VT::Str)
                    rtError(e->line, "字典的键必须是文本（C++ 版 v0.1 限制）");
                d->items[k.str] = eval(p.second, scope);
            }
            return vDict(d);
        }
    }
    rtError(e->line, "未知表达式节点");
}

static bool cacheableBuiltin(const string& name) {
    auto it = functionModules().find(name);
    if (it == functionModules().end()) return false;
    const string& lib = it->second;
    if (lib == "数学库" || lib == "图形库" || lib == "物理库") return true;
    if (lib == "概率库")
        return name != "随机抽样" && name != "洗牌" && name != "加权选择";
    return false;
}

static string accelKey(const string& name, const vector<Value>& args) {
    string key = name;
    for (const auto& v : args) {
        string s = toText(v, true);
        key += "|" + std::to_string(s.size()) + ":" + s;
    }
    return key;
}

Value Interpreter::callValue(const Value& callee, vector<Value>& args, int line) {
    if (callee.type != VT::Func || !callee.callable)
        rtError(line, "这个值不能调用：" + typeName(callee));
    if (!accelEnabled && !leapAuto && !memoryAuto)
        return callee.callable->call(*this, args, line);
    if (accelEnabled && callee.callable->cacheable()) {
        string key = accelKey(callee.callable->name, args);
        auto it = accelCache.find(key);
        if (it != accelCache.end()) {
            ++accelHits;
            return it->second;
        }
        Value out = callee.callable->call(*this, args, line);
        if (out.type == VT::Num || out.type == VT::Str || out.type == VT::Bool ||
            out.type == VT::None) {
            if (accelCache.size() >= 200000) accelCache.clear();
            accelCache[key] = out;
        }
        return out;
    }
    if (leapAuto) {
        auto lfn = std::dynamic_pointer_cast<Function>(callee.callable);
        if (lfn && !lfn->name.empty()) {
            string key = "leap|" + lfn->name + accelKey("", args);
            auto it = leapCache.find(key);
            if (it != leapCache.end()) {
                ++leapHits;
                return it->second;
            }
            ++leapMisses;
            Value out = lfn->call(*this, args, line);
            if (out.type == VT::Num || out.type == VT::Str || out.type == VT::Bool ||
                out.type == VT::None) {
                if (leapCache.size() >= leapLimit) leapCache.clear();
                leapCache[key] = out;
            }
            return out;
        }
    }
    if (memoryAuto && callee.type == VT::Func) {
        auto fn = std::dynamic_pointer_cast<Function>(callee.callable);
        if (fn && !fn->name.empty()) {
            string key = "auto|" + fn->name + accelKey("", args);
            auto it = memoryStore.find(key);
            if (it != memoryStore.end()) {
                ++memoryHits;
                return it->second;
            }
            auto cit = memoryCompressed.find(key);
            if (cit != memoryCompressed.end()) {
                string raw = decompressMemoryText(cit->second, line);
                JsonReader reader(raw, line);
                Value v = reader.parse();
                memoryStore[key] = v;
                memoryCompressed.erase(cit);
                memoryOrigSize.erase(key);
                ++memoryHits;
                return v;
            }
            Value out = callee.callable->call(*this, args, line);
            if (out.type == VT::Num || out.type == VT::Str || out.type == VT::Bool ||
                out.type == VT::None) {
                if (memoryCompress && isPulled(key)) {
                    string raw = jsonDump(out, line);
                    buildMemoryDict(raw);
                    memoryOrigSize[key] = raw.size();
                    memoryCompressed[key] = compressMemoryText(raw);
                } else {
                    memoryStore[key] = out;
                }
            }
            return out;
        }
    }
    return callee.callable->call(*this, args, line);
}

void Interpreter::assignTarget(const ExprPtr& target, const Value& val,
                               shared_ptr<Scope> scope) {
    if (target->k == Expr::K::Var) {
        scope->vars[target->text] = val;
        return;
    }
    if (target->k == Expr::K::Index) {
        Value obj = eval(target->args[0], scope);
        Value idx = eval(target->args[1], scope);
        setIndex(obj, idx, val, target->line);
        return;
    }
    if (target->k == Expr::K::Slice)
        rtError(target->line, "切片不能直接赋值；请用循环或重建列表");
    rtError(target->line, "赋值左边只能是变量或索引");
}

Value Interpreter::getIndex(const Value& obj, const Value& idx, int line) {
    if (obj.type == VT::List && obj.list) {
        long long i = needIndex(idx, "列表下标", line);
        long long n = (long long)obj.list->items.size();
        if (i < 0) i += n;
        if (i < 0 || i >= n)
            rtError(line, "列表下标越界：" + std::to_string(i) +
                          "（长度 " + std::to_string(n) + "）");
        return obj.list->items[(size_t)i];
    }
    if (obj.type == VT::Str) {
        auto chars = utf8Chars(obj.str);
        long long i = needIndex(idx, "文本下标", line);
        long long n = (long long)chars.size();
        if (i < 0) i += n;
        if (i < 0 || i >= n)
            rtError(line, "文本下标越界：" + std::to_string(i));
        return vStr(chars[(size_t)i]);
    }
    if (obj.type == VT::Dict && obj.dict) {
        if (idx.type != VT::Str) rtError(line, "字典的键必须是文本");
        auto it = obj.dict->items.find(idx.str);
        if (it == obj.dict->items.end())
            rtError(line, "字典里没有键「" + idx.str + "」");
        return it->second;
    }
    if (obj.type == VT::Range) {
        double span = (obj.rstop - obj.rstart) / obj.rstep;
        long long n = span <= 0 ? 0 : (long long)std::ceil(span - 1e-12);
        long long i = needIndex(idx, "范围下标", line);
        if (i < 0) i += n;
        if (i < 0 || i >= n) rtError(line, "范围下标越界");
        return vNum(obj.rstart + (double)i * obj.rstep);
    }
    rtError(line, typeName(obj) + "不能用 [ ] 取下标");
}

void Interpreter::setIndex(Value& obj, const Value& idx, const Value& val, int line) {
    if (obj.type == VT::List && obj.list) {
        long long i = needIndex(idx, "列表下标", line);
        long long n = (long long)obj.list->items.size();
        if (i < 0) i += n;
        if (i < 0 || i >= n)
            rtError(line, "列表下标越界：" + std::to_string(i));
        obj.list->items[(size_t)i] = val;
        return;
    }
    if (obj.type == VT::Dict && obj.dict) {
        if (idx.type != VT::Str) rtError(line, "字典的键必须是文本");
        obj.dict->items[idx.str] = val;
        return;
    }
    if (obj.type == VT::Str)
        rtError(line, "文本不能修改");
    rtError(line, typeName(obj) + "不能用 [ ] 赋值");
}

// 函数调用：返回信号、循环外中断、错误调用栈
Value Function::call(Interpreter& vm, vector<Value>& args, int callLine) {
    if (vm.callStats && !name.empty()) ++vm.callCounts[name];
    if (args.size() != params.size()) {
        throw WenError("运行时错误", callLine, 0,
                       "函数「" + name + "」需要 " + std::to_string(params.size()) +
                       " 个参数，收到 " + std::to_string(args.size()) + " 个");
    }
    auto scope = std::make_shared<Scope>(closure);
    for (size_t i = 0; i < params.size(); ++i) scope->vars[params[i]] = args[i];
    try {
        for (const auto& st : body) vm.exec(st, scope);
    } catch (ReturnSignal& r) {
        return r.value;
    } catch (BreakSignal&) {
        throw WenError("运行时错误", callLine, 0, "「中断」只能用在循环里");
    } catch (ContinueSignal&) {
        throw WenError("运行时错误", callLine, 0, "「继续」只能用在循环里");
    } catch (WenError& e) {
        if (e.frames.size() < 50)
            e.frames.push_back("在「" + name + "」（第 " +
                               std::to_string(callLine) + " 行调用）");
        throw;
    }
    return vNone();
}

Value Builtin::call(Interpreter& vm, vector<Value>& args, int line) {
    if (vm.callStats && !name.empty()) ++vm.callCounts[name];
    string lib;
    if (moduleKnown) {
        lib = module;
    } else {
        auto mit = functionModules().find(name);
        if (mit != functionModules().end()) lib = mit->second;
    }
    if (!lib.empty() && !vm.loadedModules.count(lib)) {
        throw WenError("运行时错误", line, 0,
                       "「" + name + "」属于「" + lib +
                       "」，请先在文件开头写「依赖 " + lib + "」");
    }
    if ((int)args.size() < minArgs ||
        (maxArgs >= 0 && (int)args.size() > maxArgs)) {
        string need = maxArgs < 0
                          ? ("至少 " + std::to_string(minArgs))
                          : (std::to_string(minArgs) + " 到 " + std::to_string(maxArgs));
        throw WenError("运行时错误", line, 0,
                       "「" + name + "」需要 " + need + " 个参数，收到 " +
                       std::to_string(args.size()) + " 个");
    }
    try {
        return fn(vm, args, line);
    } catch (WenError&) {
        throw;
    } catch (std::exception& e) {
        throw WenError("运行时错误", line, 0,
                       "内置函数「" + name + "」出错：" + e.what());
    }
}

// ============================================================
// 10. 内置函数与数学库
// ============================================================

static int processSaveBlocks(const string& src, bool write, string& out,
                             vector<WenError>& errors);

static string xmlEscape(const string& s) {
    string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c; break;
        }
    }
    return out;
}

static void ensureParentDirectory(const string& path) {
    fs::path p = fs::u8path(path);
    if (!p.has_parent_path()) return;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) throw IoError("无法创建目录：" + p.parent_path().u8string());
}

static string svgStart(const string& title, int w, int h) {
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w
      << "\" height=\"" << h << "\" viewBox=\"0 0 " << w << " " << h << "\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    o << "<text x=\"" << w / 2
      << "\" y=\"30\" text-anchor=\"middle\" font-size=\"20\" "
         "font-family=\"Microsoft YaHei, sans-serif\" fill=\"#222\">"
      << xmlEscape(title) << "</text>\n";
    return o.str();
}

static string svgEnd() { return "</svg>\n"; }

static double combCount(long long n, long long k) {
    if (n < 0 || k < 0 || k > n) return 0;
    if (k > n - k) k = n - k;
    double r = 1;
    for (long long i = 1; i <= k; ++i) r = r * (double)(n - k + i) / (double)i;
    return r;
}

// 概率库共用：权重校验/归一化；随机抽样和洗牌共用洗牌逻辑。
static vector<double> normalizedWeights(const shared_ptr<List>& w, const string& where, int line) {
    if (w->items.empty())
        throw WenError("运行时错误", line, 0, where + "的权重列表不能为空");
    vector<double> out;
    double sum = 0;
    for (const auto& v : w->items) {
        double x = needNum(v, where, line);
        if (x < 0) throw WenError("运行时错误", line, 0, where + "的权重不能为负");
        out.push_back(x);
        sum += x;
    }
    if (sum <= 0)
        throw WenError("运行时错误", line, 0, where + "的权重总和必须大于 0");
    for (double& x : out) x /= sum;
    return out;
}

static vector<Value> shuffledValues(const vector<Value>& items, std::mt19937& rng) {
    vector<Value> out = items;
    std::shuffle(out.begin(), out.end(), rng);
    return out;
}

static void writeRenderSvg(const string& path, const string& svg, int line) {
    try {
        ensureParentDirectory(path);
        writeWholeFile(path, svg, false);
    } catch (IoError& e) {
        throw WenError("文件错误", line, 0, e.what());
    }
}

// ============================================================
// 渲染库辅助：面、面积、均匀布点、SVG 片段（纯自研，不依赖外部库）
// ============================================================
struct RenderPoint {
    double x = 0;
    double y = 0;
};

static vector<RenderPoint> renderVerticesFromList(const Value& v, int line) {
    auto l = needList(v, "顶点列表", line);
    vector<RenderPoint> pts;
    for (const auto& item : l->items) {
        auto p = needList(item, "顶点", line);
        if (p->items.size() == 2) {
            pts.push_back({needNum(p->items[0], "顶点 x", line),
                           needNum(p->items[1], "顶点 y", line)});
        } else if (p->items.size() == 3) {
            // 三维顶点在 2D 后端里做默认透视投影；深度排序请用「渲染三维」。
            double x = needNum(p->items[0], "顶点 x", line);
            double y = needNum(p->items[1], "顶点 y", line);
            double z = needNum(p->items[2], "顶点 z", line);
            double zz = z + 8.0;
            if (zz <= 0.1) throw WenError("运行时错误", line, 0, "三维顶点在相机后面");
            pts.push_back({400.0 + x * 520.0 / zz, 300.0 - y * 520.0 / zz});
        } else {
            throw WenError("运行时错误", line, 0, "顶点必须是 [x, y] 或 [x, y, z]");
        }
    }
    if (pts.size() < 3)
        throw WenError("运行时错误", line, 0, "面至少需要 3 个顶点");
    return pts;
}

static vector<RenderPoint> renderFaceVertices(const Value& v, int line) {
    auto d = needDict(v, "面", line);
    auto it = d->items.find("顶点");
    if (it == d->items.end())
        throw WenError("运行时错误", line, 0, "这个面没有顶点；请用「新建面」创建");
    return renderVerticesFromList(it->second, line);
}

static double renderPolygonArea(const vector<RenderPoint>& p) {
    double sum = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % p.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::fabs(sum) * 0.5;
}

static RenderPoint renderCentroid(const vector<RenderPoint>& p) {
    double a2 = 0, cx = 0, cy = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % p.size()];
        double cross = a.x * b.y - b.x * a.y;
        a2 += cross;
        cx += (a.x + b.x) * cross;
        cy += (a.y + b.y) * cross;
    }
    if (std::fabs(a2) < 1e-12) {
        double sx = 0, sy = 0;
        for (const auto& q : p) {
            sx += q.x;
            sy += q.y;
        }
        return {sx / (double)p.size(), sy / (double)p.size()};
    }
    return {cx / (3.0 * a2), cy / (3.0 * a2)};
}

static Value makeFaceValue(const vector<RenderPoint>& pts) {
    auto d = std::make_shared<Dict>();
    auto vertices = std::make_shared<List>();
    for (const auto& p : pts) {
        auto one = std::make_shared<List>();
        one->items.push_back(vNum(p.x));
        one->items.push_back(vNum(p.y));
        vertices->items.push_back(vList(one));
    }
    d->items["顶点"] = vList(vertices);
    d->items["面积"] = vNum(renderPolygonArea(pts));
    auto c = renderCentroid(pts);
    auto center = std::make_shared<List>();
    center->items.push_back(vNum(c.x));
    center->items.push_back(vNum(c.y));
    d->items["中心"] = vList(center);
    double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (const auto& p : pts) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    auto box = std::make_shared<List>();
    box->items.push_back(vNum(minx));
    box->items.push_back(vNum(miny));
    box->items.push_back(vNum(maxx));
    box->items.push_back(vNum(maxy));
    d->items["边界"] = vList(box);
    return vDict(d);
}

static Value renderPointsValue(const vector<RenderPoint>& pts) {
    auto vertices = std::make_shared<List>();
    for (const auto& p : pts) {
        auto one = std::make_shared<List>();
        one->items.push_back(vNum(p.x));
        one->items.push_back(vNum(p.y));
        vertices->items.push_back(vList(one));
    }
    return vList(vertices);
}

struct Render3Point {
    double x = 0, y = 0, z = 0;
};

static vector<Render3Point> render3Vertices(const Value& v, int line) {
    auto l = needList(v, "三维顶点", line);
    vector<Render3Point> pts;
    for (const auto& item : l->items) {
        auto p = needList(item, "三维顶点", line);
        if (p->items.size() != 3)
            throw WenError("运行时错误", line, 0, "三维顶点必须是 [x, y, z]");
        pts.push_back({needNum(p->items[0], "x", line),
                       needNum(p->items[1], "y", line),
                       needNum(p->items[2], "z", line)});
    }
    if (pts.size() < 3)
        throw WenError("运行时错误", line, 0, "三维面至少需要 3 个顶点");
    return pts;
}

static Value make3Face(const vector<Render3Point>& pts, const string& color) {
    auto d = std::make_shared<Dict>();
    d->items["类型"] = vStr("三维面");
    auto vs = std::make_shared<List>();
    for (const auto& p : pts) {
        auto one = std::make_shared<List>();
        one->items.push_back(vNum(p.x));
        one->items.push_back(vNum(p.y));
        one->items.push_back(vNum(p.z));
        vs->items.push_back(vList(one));
    }
    d->items["顶点"] = vList(vs);
    d->items["颜色"] = vStr(color);
    return vDict(d);
}

static void collect3Faces(const Value& v, vector<Value>& out, int line) {
    if (v.type == VT::Dict && v.dict) {
        auto it = v.dict->items.find("类型");
        if (it != v.dict->items.end() && toText(it->second) == "三维面") {
            out.push_back(v);
            return;
        }
        throw WenError("运行时错误", line, 0, "不是三维面");
    }
    if (v.type == VT::List && v.list) {
        for (const auto& item : v.list->items) collect3Faces(item, out, line);
        return;
    }
    throw WenError("运行时错误", line, 0, "需要三维面或三维面列表");
}

static Value make3Box(double cx, double cy, double cz, double s, const string& color) {
    double h = s * 0.5;
    auto list = std::make_shared<List>();
    auto face = [&](std::initializer_list<Render3Point> pts) {
        list->items.push_back(make3Face(vector<Render3Point>(pts), color));
    };
    face({{cx-h,cy-h,cz-h},{cx+h,cy-h,cz-h},{cx+h,cy+h,cz-h},{cx-h,cy+h,cz-h}});
    face({{cx-h,cy-h,cz+h},{cx-h,cy+h,cz+h},{cx+h,cy+h,cz+h},{cx+h,cy-h,cz+h}});
    face({{cx-h,cy-h,cz-h},{cx-h,cy+h,cz-h},{cx-h,cy+h,cz+h},{cx-h,cy-h,cz+h}});
    face({{cx+h,cy-h,cz-h},{cx+h,cy-h,cz+h},{cx+h,cy+h,cz+h},{cx+h,cy+h,cz-h}});
    face({{cx-h,cy-h,cz-h},{cx-h,cy-h,cz+h},{cx+h,cy-h,cz+h},{cx+h,cy-h,cz-h}});
    face({{cx-h,cy+h,cz-h},{cx+h,cy+h,cz-h},{cx+h,cy+h,cz+h},{cx-h,cy+h,cz+h}});
    return vList(list);
}

static Value make3Sphere(double cx, double cy, double cz, double r, int segs,
                         const string& color) {
    if (segs < 3) segs = 3;
    if (segs > 64) segs = 64;
    auto list = std::make_shared<List>();
    const double pi = std::acos(-1.0);
    auto at = [&](int i, int j) {
        double th = pi * (double)i / segs;
        double ph = 2.0 * pi * (double)j / segs;
        return Render3Point{cx + r * std::sin(th) * std::cos(ph),
                            cy + r * std::cos(th),
                            cz + r * std::sin(th) * std::sin(ph)};
    };
    for (int i = 0; i < segs; ++i)
        for (int j = 0; j < segs; ++j) {
            auto a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            list->items.push_back(make3Face({a, b, c, d}, color));
        }
    return vList(list);
}
static bool renderPointInside(const vector<RenderPoint>& poly, double x, double y) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        double xi = poly[i].x, yi = poly[i].y;
        double xj = poly[j].x, yj = poly[j].y;
        bool cross = ((yi > y) != (yj > y)) &&
                     (x < (xj - xi) * (y - yi) / (yj - yi + 1e-300) + xi);
        if (cross) inside = !inside;
    }
    return inside;
}

static double renderHalton(int index, int base) {
    double f = 1, r = 0;
    while (index > 0) {
        f /= base;
        r += f * (index % base);
        index /= base;
    }
    return r;
}

// 在面内均匀布 N 个点：Halton 候选 + 最远点贪心，视觉上对等、不堆在一起。
static vector<RenderPoint> renderDistribute(const vector<RenderPoint>& poly, int n,
                                            double minx, double miny,
                                            double maxx, double maxy, int line) {
    vector<RenderPoint> cand;
    int tries = std::max(n * 150, 800);
    for (int i = 1; i <= tries; ++i) {
        double x = minx + renderHalton(i, 2) * (maxx - minx);
        double y = miny + renderHalton(i, 3) * (maxy - miny);
        if (renderPointInside(poly, x, y)) cand.push_back({x, y});
    }
    if ((int)cand.size() < n)
        throw WenError("运行时错误", line, 0,
                       "面太小，放不下 " + std::to_string(n) + " 个涂鸦");
    vector<char> used(cand.size(), 0);
    vector<RenderPoint> chosen;
    chosen.push_back(cand[0]);
    used[0] = 1;
    while ((int)chosen.size() < n) {
        int best = -1;
        double bestDist = -1;
        for (size_t i = 0; i < cand.size(); ++i) {
            if (used[i]) continue;
            double minD = std::numeric_limits<double>::max();
            for (const auto& c : chosen) {
                double dx = cand[i].x - c.x, dy = cand[i].y - c.y;
                minD = std::min(minD, dx * dx + dy * dy);
            }
            if (minD > bestDist) {
                bestDist = minD;
                best = (int)i;
            }
        }
        if (best < 0) break;
        used[best] = 1;
        chosen.push_back(cand[best]);
    }
    return chosen;
}

static string cppFloatLit(double v) {
    std::ostringstream s;
    s << std::setprecision(8) << v;
    string t = s.str();
    if (t.find('.') == string::npos && t.find('e') == string::npos &&
        t.find('E') == string::npos)
        t += ".0";
    return t;
}

static std::array<float, 3> renderHexRgb(const string& c) {    std::array<float, 3> rgb{0.2f, 0.2f, 0.2f};
    if (c.size() == 7 && c[0] == '#') {
        auto hex = [&](int i) {
            return (float)std::strtol(c.substr(i, 2).c_str(), nullptr, 16) / 255.0f;
        };
        rgb = {hex(1), hex(3), hex(5)};
    }
    return rgb;
}

static string renderColor(const string& c, const string& fallback) {    if (c.size() == 4 || c.size() == 7) {
        if (c[0] == '#') {
            bool ok = true;
            for (size_t i = 1; i < c.size(); ++i)
                if (!std::isxdigit((unsigned char)c[i])) ok = false;
            if (ok) return c;
        }
    }
    bool letters = !c.empty();
    for (unsigned char ch : c)
        if (!std::isalpha(ch)) letters = false;
    if (letters) return c;
    return fallback;
}

static string renderPointsAttr(const vector<RenderPoint>& pts) {
    std::ostringstream o;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) o << " ";
        o << std::setprecision(6) << pts[i].x << "," << pts[i].y;
    }
    return o.str();
}

static string renderMotif(const string& pattern, double x, double y, double r) {
    std::ostringstream o;
    o << std::setprecision(4);
    if (pattern == "星") {
        o << "<polygon points=\"";
        for (int k = 0; k < 10; ++k) {
            double ang = -std::acos(-1.0) / 2.0 + k * std::acos(-1.0) / 5.0;
            double rr = (k % 2 == 0) ? r : r * 0.42;
            if (k) o << " ";
            o << (x + std::cos(ang) * rr) << "," << (y + std::sin(ang) * rr);
        }
        o << "\" fill=\"none\" stroke=\"#2b2b2b\" stroke-width=\"1.2\"/>\n";
    } else if (pattern == "波纹") {
        for (int k = 1; k <= 3; ++k)
            o << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << (r * k / 3.0)
              << "\" fill=\"none\" stroke=\"#2b2b2b\" stroke-width=\"1\"/>\n";
    } else if (pattern == "螺旋") {
        o << "<polyline points=\"";
        const int steps = 48;
        for (int k = 0; k <= steps; ++k) {
            double t = (double)k / steps;
            double ang = t * 5.0 * std::acos(-1.0);
            double rr = r * (0.15 + 0.85 * t);
            if (k) o << " ";
            o << (x + std::cos(ang) * rr) << "," << (y + std::sin(ang) * rr);
        }
        o << "\" fill=\"none\" stroke=\"#2b2b2b\" stroke-width=\"1.1\"/>\n";
    } else {
        o << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << r
          << "\" fill=\"none\" stroke=\"#2b2b2b\" stroke-width=\"1.2\"/>\n";
    }
    return o.str();
}

// 体积光：2D 形体雾 / 3D 范围雾（SVG 近似，不引外部库）
static string renderLightSvg(const vector<RenderPoint>& poly, double lx, double ly,
                             double strength, bool volumetric, const string& title,
                             double minx, double miny, double maxx, double maxy) {
    double w = (maxx - minx) + 80, h = (maxy - miny) + 110;
    auto tx = [&](double x) { return x - minx + 40; };
    auto ty = [&](double y) { return y - miny + 70; };
    vector<RenderPoint> pts;
    for (const auto& p : poly) pts.push_back({tx(p.x), ty(p.y)});
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int)w
      << "\" height=\"" << (int)h << "\" viewBox=\"0 0 " << (int)w << " " << (int)h
      << "\">\n";
    o << "<defs>\n";
    o << "  <clipPath id=\"faceClip\"><polygon points=\"" << renderPointsAttr(pts)
      << "\"/></clipPath>\n";
    o << "  <radialGradient id=\"glow\" gradientUnits=\"userSpaceOnUse\" cx=\""
      << tx(lx) << "\" cy=\"" << ty(ly) << "\" r=\"" << (w + h) << "\">\n";
    o << "    <stop offset=\"0\" stop-color=\"#ffffff\" stop-opacity=\"" << strength
      << "\"/>\n";
    o << "    <stop offset=\"0.35\" stop-color=\"#ffffff\" stop-opacity=\""
      << (strength * 0.55) << "\"/>\n";
    o << "    <stop offset=\"1\" stop-color=\"#ffffff\" stop-opacity=\"0\"/>\n";
    o << "  </radialGradient>\n";
    if (volumetric) {
        o << "  <filter id=\"fog\" x=\"-20%\" y=\"-20%\" width=\"140%\" height=\"140%\">\n";
        o << "    <feGaussianBlur stdDeviation=\"14\"/>\n";
        o << "  </filter>\n";
    }
    o << "</defs>\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"#0b1020\"/>\n";
    o << "<polygon points=\"" << renderPointsAttr(pts)
      << "\" fill=\"#101826\" stroke=\"#5a6b8c\" stroke-width=\"1.5\"/>\n";
    o << "<g clip-path=\"url(#faceClip)\"";
    if (volumetric) o << " filter=\"url(#fog)\"";
    o << ">\n";
    o << "<polygon points=\"" << renderPointsAttr(pts) << "\" fill=\"url(#glow)\"/>\n";
    if (volumetric) {
        // 多层错位，模拟“范围雾”的厚度。
        for (int k = 1; k <= 4; ++k) {
            double dx = (lx - (minx + maxx) / 2) * k * 0.02;
            double dy = (ly - (miny + maxy) / 2) * k * 0.02;
            o << "<polygon points=\"" << renderPointsAttr(pts)
              << "\" fill=\"url(#glow)\" opacity=\"" << (0.22 / k) << "\""
              << " transform=\"translate(" << dx << "," << dy << ")\"/>\n";
        }
    }
    o << "</g>\n";
    o << "<circle cx=\"" << tx(lx) << "\" cy=\"" << ty(ly)
      << "\" r=\"5\" fill=\"#fff6c8\"/>\n";
    o << "<text x=\"" << (int)(w / 2) << "\" y=\"30\" text-anchor=\"middle\" "
         "font-size=\"18\" font-family=\"Microsoft YaHei, sans-serif\" fill=\"#e8eefc\">"
      << xmlEscape(title) << "</text>\n";
    o << "</svg>\n";
    return o.str();
}

static string externalTarget(const string& src) {
    std::istringstream in(src);
    string line;
    while (std::getline(in, line)) {
        string t = trimText(line);
        if (t.empty()) continue;
        if (t[0] != '#') return "";
        t = trimText(t.substr(1));
        if (t.rfind("外派", 0) != 0) return "";
        t = trimText(t.substr(6));
        if (!t.empty() && (t[0] == ':' || startsWith(t, 0, "："))) {
            t = trimText(t.substr(t[0] == ':' ? 1 : 3));
        }
        size_t p = t.find_first_of(" \t");
        return p == string::npos ? t : t.substr(0, p);
    }
    return "";
}

static string pyQuote(const string& s) {
    string out = "'";
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\'') out += "\\'";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    out += "'";
    return out;
}

struct PyEmitter {
    const vector<StmtPtr>& program;
    vector<string> diags;
    std::ostringstream out;
    int indent = 0;
    bool usedMath = false;
    bool usedRandom = false;

    explicit PyEmitter(const vector<StmtPtr>& p) : program(p) {}

    void diag(int line, const string& msg) {
        diags.push_back("第 " + std::to_string(line) + " 行：" + msg);
    }
    void line(const string& s) {
        out << string(indent * 4, ' ') << s << "\n";
    }
    string expr(const ExprPtr& e) {
        if (!e) return "None";
        switch (e->k) {
            case Expr::K::Num: {
                if (std::isnan(e->num)) return "float('nan')";
                if (std::isinf(e->num)) return e->num < 0 ? "-float('inf')" : "float('inf')";
                std::ostringstream s;
                s << std::setprecision(17) << e->num;
                return s.str();
            }
            case Expr::K::Str: return pyQuote(e->text);
            case Expr::K::Bool: return e->b ? "True" : "False";
            case Expr::K::None: return "None";
            case Expr::K::Var: {
                if (e->text == "圆周率" || e->text == "π") { usedMath = true; return "math.pi"; }
                if (e->text == "自然常数") { usedMath = true; return "math.e"; }
                return e->text;
            }
            case Expr::K::Unary: {
                string a = expr(e->args[0]);
                if (e->op == "非") return "(not " + a + ")";
                return "(" + e->op + a + ")";
            }
            case Expr::K::Binary: {
                string a = expr(e->args[0]), b = expr(e->args[1]);
                string op = e->op;
                if (op == "且") op = "and";
                else if (op == "或") op = "or";
                else if (op == "包含") op = "in";
                else if (op == "不包含") { return "(" + a + " not in " + b + ")"; }
                return "(" + a + " " + op + " " + b + ")";
            }
            case Expr::K::Call: {
                string callee = expr(e->args[0]);
                string name = e->args[0]->k == Expr::K::Var ? e->args[0]->text : "";
                static const std::map<string, string> pyNames = {
                    {"长度", "len"}, {"范围", "range"}, {"绝对值", "abs"},
                    {"取整", "int"}, {"最大", "max"}, {"最小", "min"},
                    {"求和", "sum"}, {"幂", "pow"}, {"类型", "type"},
                    {"打印", "print"}};
                static const std::map<string, string> mathNames = {
                    {"平方根", "sqrt"}, {"正弦", "sin"}, {"余弦", "cos"},
                    {"正切", "tan"}, {"对数", "log"}, {"对数10", "log10"},
                    {"指数", "exp"}, {"反余弦", "acos"}, {"反正弦", "asin"},
                    {"反正切", "atan"}, {"双曲正弦", "sinh"}, {"双曲余弦", "cosh"},
                    {"双曲正切", "tanh"}, {"向上取整", "ceil"}, {"向下取整", "floor"}};
                auto mit = mathNames.find(name);
                if (mit != mathNames.end()) {
                    usedMath = true;
                    callee = "math." + mit->second;
                } else {
                    auto it = pyNames.find(name);
                    if (it != pyNames.end()) callee = it->second;
                    else if (name == "随机") { usedRandom = true; callee = "random.random"; }
                    else if (name == "随机整数") { usedRandom = true; callee = "random.randint"; }
                    else if (!name.empty() && functionModules().count(name) &&
                             !pyNames.count(name)) {
                        diag(e->line, "内置函数「" + name + "」没有 Python 映射");
                    }
                }
                string s = callee + "(";
                for (size_t i = 1; i < e->args.size(); ++i) {
                    if (i > 1) s += ", ";
                    s += expr(e->args[i]);
                }
                return s + ")";
            }
            case Expr::K::Index:
                return expr(e->args[0]) + "[" + expr(e->args[1]) + "]";
            case Expr::K::Slice: {
                string o = expr(e->args[0]);
                string a = e->args.size() > 1 && e->args[1] ? expr(e->args[1]) : "";
                string b = e->args.size() > 2 && e->args[2] ? expr(e->args[2]) : "";
                string c = e->args.size() > 3 && e->args[3] ? expr(e->args[3]) : "";
                return o + "[" + a + ":" + b + (e->args.size() > 3 ? ":" + c : "") + "]";
            }
            case Expr::K::List: {
                string s = "[";
                for (size_t i = 0; i < e->args.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(e->args[i]);
                }
                return s + "]";
            }
            case Expr::K::Dict: {
                string s = "{";
                for (size_t i = 0; i < e->pairs.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(e->pairs[i].first) + ": " + expr(e->pairs[i].second);
                }
                return s + "}";
            }
        }
        return "None";
    }
    void block(const vector<StmtPtr>& body) {
        ++indent;
        if (body.empty()) line("pass");
        for (const auto& s : body) stmt(s);
        --indent;
    }
    void stmt(const StmtPtr& s) {
        if (!s) return;
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k == Expr::K::Var)
                    line(s->target->text + " = " + expr(s->expr));
                else if (s->target->k == Expr::K::Index)
                    line(expr(s->target) + " = " + expr(s->expr));
                else
                    diag(s->line, "外派只支持变量/下标赋值");
                break;
            case Stmt::K::Print: {
                string args;
                for (size_t i = 0; i < s->args.size(); ++i) {
                    if (i) args += ", ";
                    args += expr(s->args[i]);
                }
                line("print(" + args + ")");
                break;
            }
            case Stmt::K::If:
                for (size_t i = 0; i < s->branches.size(); ++i) {
                    line(string(i == 0 ? "if " : "elif ") + expr(s->branches[i].first) + ":");
                    block(s->branches[i].second);
                }
                if (!s->elseBody.empty()) {
                    line("else:");
                    block(s->elseBody);
                }
                break;
            case Stmt::K::While:
                line("while " + expr(s->expr) + ":");
                block(s->body);
                break;
            case Stmt::K::For:
                if (!s->name2.empty())
                    line("for " + s->name + ", " + s->name2 + " in " + expr(s->expr) + ".items():");
                else
                    line("for " + s->name + " in " + expr(s->expr) + ":");
                block(s->body);
                break;
            case Stmt::K::Func: {
                string params;
                for (size_t i = 0; i < s->params.size(); ++i) {
                    if (i) params += ", ";
                    params += s->params[i];
                }
                line("def " + s->name + "(" + params + "):");
                block(s->body);
                break;
            }
            case Stmt::K::Return:
                line("return" + (s->expr ? " " + expr(s->expr) : ""));
                break;
            case Stmt::K::Break: line("break"); break;
            case Stmt::K::Continue: line("continue"); break;
            case Stmt::K::ExprStmt: line(expr(s->expr)); break;
            case Stmt::K::Import:
                diag(s->line, "「依赖」不翻译；请在外派目标里手动补库");
                break;
            case Stmt::K::Export:
                break;
            case Stmt::K::Try:
                line("try:");
                block(s->body);
                if (!s->name.empty()) {
                    line("except Exception as " + s->name + ":");
                    block(s->elseBody);
                }
                if (!s->finallyBody.empty()) {
                    line("finally:");
                    block(s->finallyBody);
                }
                break;
        }
    }
    string generate() {
        for (const auto& s : program) stmt(s);
        string head = "# 由「文」外派库生成 · WEN-LANG-0.1\n";
        if (usedMath) head += "import math\n";
        if (usedRandom) head += "import random\n";
        return head + out.str();
    }
};

struct JsEmitter {
    const vector<StmtPtr>& program;
    vector<string> diags;
    std::ostringstream out;
    int indent = 0;

    explicit JsEmitter(const vector<StmtPtr>& p) : program(p) {}

    void diag(int line, const string& msg) {
        diags.push_back("第 " + std::to_string(line) + " 行：" + msg);
    }
    void line(const string& s) { out << string(indent * 2, ' ') << s << "\n"; }
    string expr(const ExprPtr& e) {
        if (!e) return "null";
        switch (e->k) {
            case Expr::K::Num: {
                if (std::isnan(e->num)) return "NaN";
                if (std::isinf(e->num)) return e->num < 0 ? "-Infinity" : "Infinity";
                std::ostringstream s;
                s << std::setprecision(17) << e->num;
                return s.str();
            }
            case Expr::K::Str: return pyQuote(e->text);
            case Expr::K::Bool: return e->b ? "true" : "false";
            case Expr::K::None: return "null";
            case Expr::K::Var: {
                if (e->text == "圆周率" || e->text == "π") return "Math.PI";
                if (e->text == "自然常数") return "Math.E";
                return e->text;
            }
            case Expr::K::Unary: {
                string a = expr(e->args[0]);
                if (e->op == "非") return "(!wenTruthy(" + a + "))";
                return "(" + e->op + a + ")";
            }
            case Expr::K::Binary: {
                string a = expr(e->args[0]), b = expr(e->args[1]);
                string op = e->op;
                if (op == "且") return "(wenTruthy(" + a + ") && wenTruthy(" + b + "))";
                if (op == "或") return "(wenTruthy(" + a + ") || wenTruthy(" + b + "))";
                if (op == "包含") return "wenContains(" + b + ", " + a + ")";
                if (op == "不包含") return "(!wenContains(" + b + ", " + a + "))";
                return "(" + a + " " + op + " " + b + ")";
            }
            case Expr::K::Call: {
                string callee = expr(e->args[0]);
                string name = e->args[0]->k == Expr::K::Var ? e->args[0]->text : "";
                static const std::map<string, string> jsNames = {
                    {"长度", "wenLen"}, {"范围", "wenRange"}, {"绝对值", "Math.abs"},
                    {"取整", "Math.trunc"}, {"最大", "Math.max"}, {"最小", "Math.min"},
                    {"求和", "wenSum"}, {"幂", "Math.pow"}, {"类型", "wenType"},
                    {"打印", "console.log"}};
                static const std::map<string, string> mathNames = {
                    {"平方根", "Math.sqrt"}, {"正弦", "Math.sin"}, {"余弦", "Math.cos"},
                    {"正切", "Math.tan"}, {"对数", "Math.log"}, {"对数10", "Math.log10"},
                    {"指数", "Math.exp"}, {"反余弦", "Math.acos"}, {"反正弦", "Math.asin"},
                    {"反正切", "Math.atan"}, {"双曲正弦", "Math.sinh"},
                    {"双曲余弦", "Math.cosh"}, {"双曲正切", "Math.tanh"},
                    {"向上取整", "Math.ceil"}, {"向下取整", "Math.floor"}};
                auto mit = mathNames.find(name);
                if (mit != mathNames.end()) callee = mit->second;
                else {
                    auto it = jsNames.find(name);
                    if (it != jsNames.end()) callee = it->second;
                    else if (name == "随机") callee = "Math.random";
                    else if (name == "随机整数") callee = "wenRandInt";
                    else if (!name.empty() && functionModules().count(name) &&
                             !jsNames.count(name))
                        diag(e->line, "内置函数「" + name + "」没有 JavaScript 映射");
                }
                string s = callee + "(";
                for (size_t i = 1; i < e->args.size(); ++i) {
                    if (i > 1) s += ", ";
                    s += expr(e->args[i]);
                }
                return s + ")";
            }
            case Expr::K::Index:
                return expr(e->args[0]) + "[" + expr(e->args[1]) + "]";
            case Expr::K::Slice: {
                string o = expr(e->args[0]);
                string a = e->args.size() > 1 && e->args[1] ? expr(e->args[1]) : "0";
                string b = e->args.size() > 2 && e->args[2] ? expr(e->args[2]) : "undefined";
                string c = e->args.size() > 3 && e->args[3] ? expr(e->args[3]) : "1";
                return "wenSlice(" + o + ", " + a + ", " + b + ", " + c + ")";
            }
            case Expr::K::List: {
                string s = "[";
                for (size_t i = 0; i < e->args.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(e->args[i]);
                }
                return s + "]";
            }
            case Expr::K::Dict: {
                string s = "{";
                for (size_t i = 0; i < e->pairs.size(); ++i) {
                    if (i) s += ", ";
                    s += "[" + expr(e->pairs[i].first) + "]: " + expr(e->pairs[i].second);
                }
                return s + "}";
            }
        }
        return "null";
    }
    void block(const vector<StmtPtr>& body) {
        line("{");
        ++indent;
        for (const auto& s : body) stmt(s);
        --indent;
        line("}");
    }
    void stmt(const StmtPtr& s) {
        if (!s) return;
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k == Expr::K::Var)
                    line("var " + s->target->text + " = " + expr(s->expr) + ";");
                else if (s->target->k == Expr::K::Index)
                    line(expr(s->target) + " = " + expr(s->expr) + ";");
                else
                    diag(s->line, "外派只支持变量/下标赋值");
                break;
            case Stmt::K::Print: {
                string args;
                for (size_t i = 0; i < s->args.size(); ++i) {
                    if (i) args += ", ";
                    args += expr(s->args[i]);
                }
                line("console.log(" + args + ");");
                break;
            }
            case Stmt::K::If:
                for (size_t i = 0; i < s->branches.size(); ++i) {
                    line(string(i == 0 ? "if " : "else if ") + "(wenTruthy(" +
                         expr(s->branches[i].first) + "))");
                    block(s->branches[i].second);
                }
                if (!s->elseBody.empty()) {
                    line("else");
                    block(s->elseBody);
                }
                break;
            case Stmt::K::While:
                line("while (wenTruthy(" + expr(s->expr) + "))");
                block(s->body);
                break;
            case Stmt::K::For:
                if (!s->name2.empty())
                    line("for (const [" + s->name + ", " + s->name2 + "] of wenItems(" +
                         expr(s->expr) + "))");
                else
                    line("for (const " + s->name + " of " + expr(s->expr) + ")");
                block(s->body);
                break;
            case Stmt::K::Func: {
                string params;
                for (size_t i = 0; i < s->params.size(); ++i) {
                    if (i) params += ", ";
                    params += s->params[i];
                }
                line("function " + s->name + "(" + params + ")");
                block(s->body);
                break;
            }
            case Stmt::K::Return:
                line("return" + (s->expr ? " " + expr(s->expr) : "") + ";");
                break;
            case Stmt::K::Break: line("break;"); break;
            case Stmt::K::Continue: line("continue;"); break;
            case Stmt::K::ExprStmt: line(expr(s->expr) + ";"); break;
            case Stmt::K::Import:
                diag(s->line, "「依赖」不翻译；请在外派目标里手动补库");
                break;
            case Stmt::K::Export: break;
            case Stmt::K::Try:
                line("try");
                block(s->body);
                if (!s->name.empty()) {
                    line("catch (" + s->name + ")");
                    block(s->elseBody);
                }
                if (!s->finallyBody.empty()) {
                    line("finally");
                    block(s->finallyBody);
                }
                break;
        }
    }
    string generate() {
        for (const auto& s : program) stmt(s);
        const char* helpers =
            "function wenTruthy(v){if(v===null||v===undefined)return false;"
            "if(typeof v==='number')return v!==0;if(typeof v==='string')return v.length>0;"
            "if(Array.isArray(v))return v.length>0;"
            "if(typeof v==='object')return Object.keys(v).length>0;return !!v;}\n"
            "function wenLen(v){if(v===null||v===undefined)return 0;"
            "if(typeof v==='string')return [...v].length;"
            "if(Array.isArray(v))return v.length;"
            "if(typeof v==='object')return Object.keys(v).length;return 0;}\n"
            "function wenRange(a,b,c){let s=0,e=a,st=1;if(b!==undefined){s=a;e=b;}"
            "if(c!==undefined)st=c;let out=[];if(st>0){for(let i=s;i<e;i+=st)out.push(i);}"
            "else{for(let i=s;i>e;i+=st)out.push(i);}return out;}\n"
            "function wenItems(v){return Object.entries(v);}\n"
            "function wenContains(c,x){if(typeof c==='string')return c.includes(x);"
            "if(Array.isArray(c))return c.some(y=>JSON.stringify(y)===JSON.stringify(x));"
            "if(c&&typeof c==='object')return Object.prototype.hasOwnProperty.call(c,x);"
            "return false;}\n"
            "function wenSlice(v,a,b,c){a=a||0;if(b===undefined)b=v.length;if(c===undefined)c=1;"
            "let out=[];if(c>0){for(let i=a;i<b;i+=c)out.push(v[i]);}"
            "else{for(let i=a;i>b;i+=c)out.push(v[i]);}return out;}\n"
            "function wenSum(v){let s=0;for(const x of v)s+=x;return s;}\n"
            "function wenType(v){if(v===null||v===undefined)return '空';"
            "if(typeof v==='number')return '数字';if(typeof v==='string')return '文本';"
            "if(typeof v==='boolean')return '布尔';if(Array.isArray(v))return '列表';"
            "if(typeof v==='object')return '字典';return '未知';}\n"
            "function wenRandInt(a,b){return Math.floor(Math.random()*(b-a+1))+a;}\n";
        return "// 由「文」外派库生成 · WEN-LANG-0.1\n" + string(helpers) + "\n" + out.str();
    }
};

struct JsonEmitter {
    const vector<StmtPtr>& program;
    vector<string> diags;
    explicit JsonEmitter(const vector<StmtPtr>& p) : program(p) {}
    void diag(int line, const string& msg) {
        diags.push_back("第 " + std::to_string(line) + " 行：" + msg);
    }
    string expr(const ExprPtr& e) {
        if (!e) return "null";
        switch (e->k) {
            case Expr::K::Num: return "{\"k\":\"num\",\"v\":" + numberToString(e->num) + "}";
            case Expr::K::Str: return "{\"k\":\"str\",\"v\":" + quoteText(e->text) + "}";
            case Expr::K::Bool: return string("{\"k\":\"bool\",\"v\":") + (e->b ? "true" : "false") + "}";
            case Expr::K::None: return "{\"k\":\"none\"}";
            case Expr::K::Var: return "{\"k\":\"var\",\"name\":" + quoteText(e->text) + "}";
            case Expr::K::Unary:
                return "{\"k\":\"unary\",\"op\":" + quoteText(e->op) + ",\"a\":" + expr(e->args[0]) + "}";
            case Expr::K::Binary:
                return "{\"k\":\"binary\",\"op\":" + quoteText(e->op) + ",\"a\":" + expr(e->args[0]) +
                       ",\"b\":" + expr(e->args[1]) + "}";
            case Expr::K::Call: {
                string s = "{\"k\":\"call\",\"callee\":" + expr(e->args[0]) + ",\"args\":[";
                for (size_t i = 1; i < e->args.size(); ++i) {
                    if (i > 1) s += ",";
                    s += expr(e->args[i]);
                }
                return s + "]}";
            }
            case Expr::K::Index:
                return "{\"k\":\"index\",\"obj\":" + expr(e->args[0]) + ",\"idx\":" +
                       expr(e->args[1]) + "}";
            case Expr::K::Slice: {
                auto part = [&](size_t i) { return e->args.size() > i && e->args[i] ? expr(e->args[i]) : "null"; };
                return "{\"k\":\"slice\",\"obj\":" + expr(e->args[0]) + ",\"start\":" + part(1) +
                       ",\"stop\":" + part(2) + ",\"step\":" + part(3) + "}";
            }
            case Expr::K::List: {
                string s = "{\"k\":\"list\",\"items\":[";
                for (size_t i = 0; i < e->args.size(); ++i) {
                    if (i) s += ",";
                    s += expr(e->args[i]);
                }
                return s + "]}";
            }
            case Expr::K::Dict: {
                string s = "{\"k\":\"dict\",\"pairs\":[";
                for (size_t i = 0; i < e->pairs.size(); ++i) {
                    if (i) s += ",";
                    s += "[" + expr(e->pairs[i].first) + "," + expr(e->pairs[i].second) + "]";
                }
                return s + "]}";
            }
        }
        return "null";
    }
    string block(const vector<StmtPtr>& body) {
        string s = "[";
        for (size_t i = 0; i < body.size(); ++i) {
            if (i) s += ",";
            s += stmt(body[i]);
        }
        return s + "]";
    }
    string stmt(const StmtPtr& s) {
        if (!s) return "null";
        string head = "{\"line\":" + std::to_string(s->line) + ",\"k\":";
        switch (s->k) {
            case Stmt::K::Assign:
                return head + "\"assign\",\"target\":" + expr(s->target) + ",\"expr\":" +
                       expr(s->expr) + "}";
            case Stmt::K::Print: {
                string a = "[";
                for (size_t i = 0; i < s->args.size(); ++i) {
                    if (i) a += ",";
                    a += expr(s->args[i]);
                }
                return head + "\"print\",\"args\":" + a + "]}";
            }
            case Stmt::K::If: {
                string b = "[";
                for (size_t i = 0; i < s->branches.size(); ++i) {
                    if (i) b += ",";
                    b += "{\"cond\":" + expr(s->branches[i].first) + ",\"body\":" +
                         block(s->branches[i].second) + "}";
                }
                return head + "\"if\",\"branches\":" + b + ",\"else\":" + block(s->elseBody) + "}";
            }
            case Stmt::K::While:
                return head + "\"while\",\"cond\":" + expr(s->expr) + ",\"body\":" + block(s->body) + "}";
            case Stmt::K::For:
                return head + "\"for\",\"name\":" + quoteText(s->name) + ",\"name2\":" +
                       quoteText(s->name2) + ",\"iter\":" + expr(s->expr) + ",\"body\":" +
                       block(s->body) + "}";
            case Stmt::K::Func: {
                string p = "[";
                for (size_t i = 0; i < s->params.size(); ++i) {
                    if (i) p += ",";
                    p += quoteText(s->params[i]);
                }
                p += "]";
                return head + "\"func\",\"name\":" + quoteText(s->name) + ",\"params\":" + p +
                       ",\"body\":" + block(s->body) + "}";
            }
            case Stmt::K::Return:
                return head + "\"return\",\"expr\":" + (s->expr ? expr(s->expr) : "null") + "}";
            case Stmt::K::Break: return head + "\"break\"}";
            case Stmt::K::Continue: return head + "\"continue\"}";
            case Stmt::K::ExprStmt: return head + "\"expr\",\"expr\":" + expr(s->expr) + "}";
            case Stmt::K::Import: {
                string a = "[";
                for (size_t i = 0; i < s->imports.size(); ++i) {
                    if (i) a += ",";
                    a += quoteText(s->imports[i]);
                }
                return head + "\"import\",\"names\":" + a + "]}";
            }
            case Stmt::K::Export: {
                string a = "[";
                for (size_t i = 0; i < s->imports.size(); ++i) {
                    if (i) a += ",";
                    a += quoteText(s->imports[i]);
                }
                return head + "\"export\",\"names\":" + a + "]}";
            }
            case Stmt::K::Try:
                return head + "\"try\",\"body\":" + block(s->body) + ",\"catch\":" +
                       quoteText(s->name) + ",\"catchBody\":" + block(s->elseBody) +
                       ",\"finally\":" + block(s->finallyBody) + "}";
        }
        return "null";
    }
    string generate() {
        return "{\"target\":\"json\",\"ast\":" + block(program) + "}";
    }
};

struct JavaEmitter {
    const vector<StmtPtr>& program;
    vector<string> diags;
    std::ostringstream out;
    int indent = 1;
    set<string> locals;

    explicit JavaEmitter(const vector<StmtPtr>& p) : program(p) {}

    void diag(int line, const string& msg) {
        diags.push_back("第 " + std::to_string(line) + " 行：" + msg);
    }
    void line(const string& s) { out << string(indent * 4, ' ') << s << "\n"; }
    static string jstr(const string& s) {
        string o = "\"";
        for (char c : s) {
            if (c == '\\') o += "\\\\";
            else if (c == '"') o += "\\\"";
            else if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else if (c == '\t') o += "\\t";
            else o += c;
        }
        return o + "\"";
    }
    string expr(const ExprPtr& e) {
        if (!e) return "null";
        switch (e->k) {
            case Expr::K::Num: {
                if (std::isnan(e->num) || std::isinf(e->num)) {
                    diag(e->line, "Java 目标不支持非数/无穷");
                    return "null";
                }
                return numberToString(e->num);
            }
            case Expr::K::Str: return jstr(e->text);
            case Expr::K::Bool: return e->b ? "true" : "false";
            case Expr::K::None: return "null";
            case Expr::K::Var: {
                if (e->text == "圆周率" || e->text == "π") return "Math.PI";
                if (e->text == "自然常数") return "Math.E";
                return e->text;
            }
            case Expr::K::Unary:
                if (e->op == "非") return "(!W.truthy(" + expr(e->args[0]) + "))";
                return "(W.neg(" + expr(e->args[0]) + "))";
            case Expr::K::Binary: {
                string a = expr(e->args[0]), b = expr(e->args[1]);
                string op = e->op;
                if (op == "且") return "(W.truthy(" + a + ") && W.truthy(" + b + "))";
                if (op == "或") return "(W.truthy(" + a + ") || W.truthy(" + b + "))";
                if (op == "包含") return "W.contains(" + b + "," + a + ")";
                if (op == "不包含") return "(!W.contains(" + b + "," + a + "))";
                if (op == "+") return "W.add(" + a + "," + b + ")";
                if (op == "-") return "W.sub(" + a + "," + b + ")";
                if (op == "*") return "W.mul(" + a + "," + b + ")";
                if (op == "/") return "W.div(" + a + "," + b + ")";
                if (op == "//") return "W.idiv(" + a + "," + b + ")";
                if (op == "%") return "W.mod(" + a + "," + b + ")";
                if (op == "**") return "W.pow(" + a + "," + b + ")";
                if (op == "==") return "W.eq(" + a + "," + b + ")";
                if (op == "!=") return "(!W.eq(" + a + "," + b + "))";
                if (op == "<") return "W.lt(" + a + "," + b + ")";
                if (op == "<=") return "W.le(" + a + "," + b + ")";
                if (op == ">") return "W.gt(" + a + "," + b + ")";
                if (op == ">=") return "W.ge(" + a + "," + b + ")";
                diag(e->line, "Java 目标不支持的运算符「" + op + "」");
                return "null";
            }
            case Expr::K::Call: {
                string name = e->args[0]->k == Expr::K::Var ? e->args[0]->text : "";
                string callee = expr(e->args[0]);
                static const std::map<string, string> builtins = {
                    {"长度", "W.len"}, {"范围", "W.range"}, {"绝对值", "W.abs"},
                    {"取整", "W.intVal"}, {"最大", "W.max"}, {"最小", "W.min"},
                    {"求和", "W.sum"}, {"幂", "W.pow"}, {"类型", "W.type"},
                    {"打印", "W.print"}, {"平方根", "W.sqrt"}, {"正弦", "W.sin"},
                    {"余弦", "W.cos"}, {"正切", "W.tan"}, {"对数", "W.log"},
                    {"对数10", "W.log10"}, {"指数", "W.exp"}, {"随机", "W.random"},
                    {"随机整数", "W.randInt"}};
                auto it = builtins.find(name);
                if (it != builtins.end()) callee = it->second;
                else if (!name.empty() && functionModules().count(name))
                    diag(e->line, "内置函数「" + name + "」没有 Java 映射");
                string s = callee + "(";
                for (size_t i = 1; i < e->args.size(); ++i) {
                    if (i > 1) s += ", ";
                    s += expr(e->args[i]);
                }
                return s + ")";
            }
            case Expr::K::Index:
                return "W.index(" + expr(e->args[0]) + "," + expr(e->args[1]) + ")";
            case Expr::K::Slice:
                return "W.slice(" + expr(e->args[0]) + "," +
                       (e->args.size() > 1 && e->args[1] ? expr(e->args[1]) : "null") + "," +
                       (e->args.size() > 2 && e->args[2] ? expr(e->args[2]) : "null") + "," +
                       (e->args.size() > 3 && e->args[3] ? expr(e->args[3]) : "null") + ")";
            case Expr::K::List: {
                string s = "W.list(";
                for (size_t i = 0; i < e->args.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(e->args[i]);
                }
                return s + ")";
            }
            case Expr::K::Dict: {
                string s = "W.dict(";
                for (size_t i = 0; i < e->pairs.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(e->pairs[i].first) + ", " + expr(e->pairs[i].second);
                }
                return s + ")";
            }
        }
        return "null";
    }
    void collect(const StmtPtr& s) {
        if (!s) return;
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k == Expr::K::Var) locals.insert(s->target->text);
                break;
            case Stmt::K::If:
                for (const auto& b : s->branches)
                    for (const auto& x : b.second) collect(x);
                for (const auto& x : s->elseBody) collect(x);
                break;
            case Stmt::K::While:
                for (const auto& x : s->body) collect(x);
                break;
            case Stmt::K::For:
                for (const auto& x : s->body) collect(x);
                break;
            case Stmt::K::Try:
                for (const auto& x : s->body) collect(x);
                for (const auto& x : s->elseBody) collect(x);
                for (const auto& x : s->finallyBody) collect(x);
                break;
            default: break;
        }
    }
    void block(const vector<StmtPtr>& body) {
        line("{");
        ++indent;
        for (const auto& s : body) stmt(s);
        --indent;
        line("}");
    }
    void stmt(const StmtPtr& s) {
        if (!s) return;
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k == Expr::K::Var)
                    line(s->target->text + " = " + expr(s->expr) + ";");
                else if (s->target->k == Expr::K::Index)
                    line("W.setIndex(" + expr(s->target->args[0]) + "," +
                         expr(s->target->args[1]) + "," + expr(s->expr) + ");");
                else
                    diag(s->line, "Java 目标只支持变量/下标赋值");
                break;
            case Stmt::K::Print: {
                string a;
                for (size_t i = 0; i < s->args.size(); ++i) {
                    if (i) a += ", ";
                    a += expr(s->args[i]);
                }
                line("W.print(" + a + ");");
                break;
            }
            case Stmt::K::If:
                for (size_t i = 0; i < s->branches.size(); ++i) {
                    line(string(i == 0 ? "if (W.truthy(" : "else if (W.truthy(") +
                         expr(s->branches[i].first) + "))");
                    block(s->branches[i].second);
                }
                if (!s->elseBody.empty()) {
                    line("else");
                    block(s->elseBody);
                }
                break;
            case Stmt::K::While:
                line("while (W.truthy(" + expr(s->expr) + "))");
                block(s->body);
                break;
            case Stmt::K::For:
                if (!s->name2.empty()) {
                    line("for (Object[] kv : W.entries(" + expr(s->expr) + "))");
                    line("{");
                    ++indent;
                    line("Object " + s->name + " = kv[0];");
                    line("Object " + s->name2 + " = kv[1];");
                    for (const auto& x : s->body) stmt(x);
                    --indent;
                    line("}");
                } else {
                    line("for (Object " + s->name + " : W.iter(" + expr(s->expr) + "))");
                    block(s->body);
                }
                break;
            case Stmt::K::Func: {
                string params;
                for (size_t i = 0; i < s->params.size(); ++i) {
                    if (i) params += ", ";
                    params += "Object " + s->params[i];
                }
                line("static Object " + s->name + "(" + params + ")");
                line("{");
                ++indent;
                set<string> save = locals;
                locals.clear();
                for (const auto& x : s->body) collect(x);
                vector<string> names(locals.begin(), locals.end());
                for (const auto& n : names) line("Object " + n + ";");
                for (const auto& x : s->body) stmt(x);
                if (s->body.empty() || s->body.back()->k != Stmt::K::Return)
                    line("return null;");
                locals = save;
                --indent;
                line("}");
                break;
            }
            case Stmt::K::Return:
                line("return" + (s->expr ? " " + expr(s->expr) : " null") + ";");
                break;
            case Stmt::K::Break: line("break;"); break;
            case Stmt::K::Continue: line("continue;"); break;
            case Stmt::K::ExprStmt: line(expr(s->expr) + ";"); break;
            case Stmt::K::Import:
                diag(s->line, "「依赖」不翻译；请在外派目标里手动补库");
                break;
            case Stmt::K::Export: break;
            case Stmt::K::Try:
                line("try");
                block(s->body);
                if (!s->name.empty()) {
                    line("catch (Exception " + s->name + ")");
                    block(s->elseBody);
                }
                if (!s->finallyBody.empty()) {
                    line("finally");
                    block(s->finallyBody);
                }
                break;
        }
    }
    string generate() {
        std::ostringstream head;
        head << "// 由「文」外派库生成 · WEN-LANG-0.1\nimport java.util.*;\n\npublic class Main {\n";
        head << R"JAVA(static class W {
    static void print(Object... a) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(" "); b.append(str(a[i])); }
        System.out.println(b);
    }
    static String str(Object v) {
        if (v == null) return "空";
        if (v instanceof List) {
            StringBuilder b = new StringBuilder("[");
            List<?> l = (List<?>) v;
            for (int i = 0; i < l.size(); i++) { if (i > 0) b.append(", "); b.append(str(l.get(i))); }
            return b.append("]").toString();
        }
        if (v instanceof Map) {
            StringBuilder b = new StringBuilder("{");
            boolean first = true;
            for (Map.Entry<?,?> e : ((Map<?,?>) v).entrySet()) {
                if (!first) b.append(", "); first = false;
                b.append(str(e.getKey())).append(": ").append(str(e.getValue()));
            }
            return b.append("}").toString();
        }
        return String.valueOf(v);
    }
    static boolean truthy(Object v) {
        if (v == null) return false;
        if (v instanceof Number) return ((Number) v).doubleValue() != 0;
        if (v instanceof String) return !((String) v).isEmpty();
        if (v instanceof Collection) return !((Collection<?>) v).isEmpty();
        if (v instanceof Map) return !((Map<?,?>) v).isEmpty();
        return true;
    }
    static double d(Object v) { return ((Number) v).doubleValue(); }
    static Object neg(Object a) { return -d(a); }
    static Object add(Object a, Object b) {
        if (a instanceof String || b instanceof String) return str(a) + str(b);
        return d(a) + d(b);
    }
    static Object sub(Object a, Object b) { return d(a) - d(b); }
    static Object mul(Object a, Object b) { return d(a) * d(b); }
    static Object div(Object a, Object b) { return d(a) / d(b); }
    static Object idiv(Object a, Object b) { return Math.floor(d(a) / d(b)); }
    static Object mod(Object a, Object b) { return d(a) % d(b); }
    static Object pow(Object a, Object b) { return Math.pow(d(a), d(b)); }
    static boolean eq(Object a, Object b) { return Objects.equals(a, b); }
    static boolean lt(Object a, Object b) { return d(a) < d(b); }
    static boolean le(Object a, Object b) { return d(a) <= d(b); }
    static boolean gt(Object a, Object b) { return d(a) > d(b); }
    static boolean ge(Object a, Object b) { return d(a) >= d(b); }
    static Object len(Object v) {
        if (v instanceof String) return (double) ((String) v).length();
        if (v instanceof Collection) return (double) ((Collection<?>) v).size();
        if (v instanceof Map) return (double) ((Map<?,?>) v).size();
        return 0.0;
    }
    static Object range(Object a) { return range(0, a, 1); }
    static Object range(Object a, Object b) { return range(a, b, 1); }
    static Object range(Object a, Object b, Object c) {
        List<Object> l = new ArrayList<>();
        double s = d(a), e = d(b), st = d(c);
        if (st > 0) for (double i = s; i < e; i += st) l.add(i);
        else for (double i = s; i > e; i += st) l.add(i);
        return l;
    }
    static List<Object> list(Object... a) { return new ArrayList<>(Arrays.asList(a)); }
    static Map<Object,Object> dict(Object... kv) {
        Map<Object,Object> m = new LinkedHashMap<>();
        for (int i = 0; i + 1 < kv.length; i += 2) m.put(kv[i], kv[i + 1]);
        return m;
    }
    static Object index(Object o, Object i) {
        if (o instanceof List) return ((List<?>) o).get(((Number) i).intValue());
        if (o instanceof Map) return ((Map<?,?>) o).get(i);
        if (o instanceof String) return String.valueOf(((String) o).charAt(((Number) i).intValue()));
        return null;
    }
    static void setIndex(Object o, Object i, Object v) {
        if (o instanceof List) ((List<Object>) o).set(((Number) i).intValue(), v);
        else if (o instanceof Map) ((Map<Object,Object>) o).put(i, v);
    }
    static Object slice(Object o, Object a, Object b, Object c) {
        List<Object> src = new ArrayList<>();
        if (o instanceof String) for (char ch : ((String) o).toCharArray()) src.add(String.valueOf(ch));
        else if (o instanceof List) src.addAll((List<?>) o);
        int n = src.size();
        int st = c == null ? 1 : ((Number) c).intValue();
        int s = a == null ? (st > 0 ? 0 : n - 1) : ((Number) a).intValue();
        int e = b == null ? (st > 0 ? n : -1) : ((Number) b).intValue();
        List<Object> out = new ArrayList<>();
        if (st > 0) for (int i = s; i < e && i < n; i += st) { if (i >= 0) out.add(src.get(i)); }
        else for (int i = s; i > e && i >= 0; i += st) { if (i < n) out.add(src.get(i)); }
        return out;
    }
    static Object sum(Object o) { double s = 0; for (Object x : (List<?>) o) s += d(x); return s; }
    static Object abs(Object a) { return Math.abs(d(a)); }
    static Object intVal(Object a) { return (double) ((long) d(a)); }
    static Object sqrt(Object a) { return Math.sqrt(d(a)); }
    static Object sin(Object a) { return Math.sin(d(a)); }
    static Object cos(Object a) { return Math.cos(d(a)); }
    static Object tan(Object a) { return Math.tan(d(a)); }
    static Object log(Object a) { return Math.log(d(a)); }
    static Object log10(Object a) { return Math.log10(d(a)); }
    static Object exp(Object a) { return Math.exp(d(a)); }
    static Object random() { return Math.random(); }
    static Object randInt(Object a, Object b) { return (double) ((int) (d(a) + Math.random() * (d(b) - d(a) + 1))); }
    static Object max(Object a, Object b) { return Math.max(d(a), d(b)); }
    static Object min(Object a, Object b) { return Math.min(d(a), d(b)); }
    static Object type(Object v) {
        if (v == null) return "空";
        if (v instanceof Number) return "数字";
        if (v instanceof String) return "文本";
        if (v instanceof Boolean) return "布尔";
        if (v instanceof List) return "列表";
        if (v instanceof Map) return "字典";
        return "未知";
    }
    static Iterable<?> iter(Object o) {
        if (o instanceof Iterable) return (Iterable<?>) o;
        if (o instanceof String) {
            List<Object> l = new ArrayList<>();
            for (char c : ((String) o).toCharArray()) l.add(String.valueOf(c));
            return l;
        }
        return new ArrayList<>();
    }
    static Iterable<Object[]> entries(Object o) {
        List<Object[]> l = new ArrayList<>();
        if (o instanceof Map) for (Map.Entry<?,?> e : ((Map<?,?>) o).entrySet()) l.add(new Object[]{e.getKey(), e.getValue()});
        return l;
    }
    static boolean contains(Object c, Object x) {
        if (c instanceof String) return ((String) c).contains(str(x));
        if (c instanceof Collection) return ((Collection<?>) c).contains(x);
        if (c instanceof Map) return ((Map<?,?>) c).containsKey(x);
        return false;
    }
}
)JAVA";
        locals.clear();
        for (const auto& s : program) collect(s);
        vector<string> names(locals.begin(), locals.end());
        // 顶层函数
        for (const auto& s : program) {
            if (s->k == Stmt::K::Func) {
                out.str("");
                out.clear();
                indent = 1;
                stmt(s);
                head << out.str();
            }
        }
        head << "\n    public static void main(String[] args) throws Exception {\n";
        for (const auto& n : names) head << "        Object " << n << ";\n";
        for (const auto& s : program)
            if (s->k != Stmt::K::Func && s->k != Stmt::K::Import) {
                out.str("");
                out.clear();
                indent = 2;
                stmt(s);
                head << out.str();
            }
        head << "    }\n}\n";
        return head.str();
    }
};

static double parallelReduceNums(const vector<double>& xs, int mode) {
    if (xs.empty()) return 0;
    unsigned hw = std::thread::hardware_concurrency();
    size_t threads = std::max(1u, std::min<unsigned>(hw ? hw : 1, (unsigned)xs.size()));
    vector<double> parts(threads, mode == 2 ? std::numeric_limits<double>::max() : 0);
    if (mode == 1)
        for (double& p : parts) p = -std::numeric_limits<double>::max();
    vector<std::thread> pool;
    for (size_t t = 0; t < threads; ++t) {
        pool.emplace_back([&, t]() {
            size_t begin = xs.size() * t / threads;
            size_t end = xs.size() * (t + 1) / threads;
            double acc = parts[t];
            for (size_t i = begin; i < end; ++i) {
                if (mode == 0) acc += xs[i];
                else if (mode == 1) acc = std::max(acc, xs[i]);
                else acc = std::min(acc, xs[i]);
            }
            parts[t] = acc;
        });
    }
    for (auto& th : pool) th.join();
    double out = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        if (mode == 0) out += parts[i];
        else if (mode == 1) out = std::max(out, parts[i]);
        else out = std::min(out, parts[i]);
    }
    return out;
}
static string translateToCppSource(const vector<StmtPtr>& program, vector<string>& diags);

struct TranslationTarget {
    int line = 0;
    string name;
};

static vector<TranslationTarget> translationTargets(const string& src) {
    vector<TranslationTarget> out;
    std::istringstream in(src);
    string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        string t = trimText(line);
        if (t.empty() || t[0] != '#') continue;
        t = trimText(t.substr(1));
        if (t.rfind("翻译", 0) != 0) continue;
        t = trimText(t.substr(6));
        if (!t.empty() && (t[0] == ':' || startsWith(t, 0, "：")))
            t = trimText(t.substr(t[0] == ':' ? 1 : 3));
        size_t p = t.find_first_of(" \t");
        string name = p == string::npos ? t : t.substr(0, p);
        if (name == "bytecode" || name == "bc") name = "字节码";
        if (name == "cpp" || name == "c++" || name == "C++") name = "C++";
        if (name == "asm" || name == "assembly") name = "汇编";
        out.push_back({n, name});
    }
    return out;
}

struct StackIns {
    string op;
    string jsonArg;
    string asmArg;
    int target = -1;
};

struct StackEmitter {
    const vector<StmtPtr>& program;
    vector<string> diags;
    vector<StackIns> code;

    explicit StackEmitter(const vector<StmtPtr>& p) : program(p) {}

    void diag(int line, const string& msg) {
        diags.push_back("第 " + std::to_string(line) + " 行：" + msg);
    }
    int emit(const string& op, const string& jsonArg = "", const string& asmArg = "") {
        code.push_back({op, jsonArg, asmArg, -1});
        return (int)code.size() - 1;
    }
    void patch(int at, int target) { code[(size_t)at].target = target; }
    void expr(const ExprPtr& e) {
        if (!e) { emit("push", "\"v\":null", "PUSH null"); return; }
        switch (e->k) {
            case Expr::K::Num:
                emit("push", "\"v\":" + numberToString(e->num), "PUSH " + numberToString(e->num));
                break;
            case Expr::K::Str:
                emit("push", "\"v\":" + quoteText(e->text), "PUSH " + pyQuote(e->text));
                break;
            case Expr::K::Bool:
                emit("push", string("\"v\":") + (e->b ? "true" : "false"), e->b ? "PUSH true" : "PUSH false");
                break;
            case Expr::K::None:
                emit("push", "\"v\":null", "PUSH null");
                break;
            case Expr::K::Var:
                emit("load", "\"name\":" + quoteText(e->text), "LOAD " + e->text);
                break;
            case Expr::K::Unary:
                expr(e->args[0]);
                if (e->op == "非") emit("not", "", "NOT");
                else if (e->op == "-") emit("neg", "", "NEG");
                else diag(e->line, "字节码/汇编不支持的运算符「" + e->op + "」");
                break;
            case Expr::K::Binary: {
                expr(e->args[0]);
                expr(e->args[1]);
                string op = e->op;
                if (op == "+") emit("add", "", "ADD");
                else if (op == "-") emit("sub", "", "SUB");
                else if (op == "*") emit("mul", "", "MUL");
                else if (op == "/") emit("div", "", "DIV");
                else if (op == "//") emit("idiv", "", "IDIV");
                else if (op == "%") emit("mod", "", "MOD");
                else if (op == "**") emit("pow", "", "POW");
                else if (op == "==") emit("eq", "", "EQ");
                else if (op == "!=") emit("ne", "", "NE");
                else if (op == "<") emit("lt", "", "LT");
                else if (op == "<=") emit("le", "", "LE");
                else if (op == ">") emit("gt", "", "GT");
                else if (op == ">=") emit("ge", "", "GE");
                else if (op == "且") emit("and", "", "AND");
                else if (op == "或") emit("or", "", "OR");
                else diag(e->line, "字节码/汇编不支持的运算符「" + op + "」");
                break;
            }
            case Expr::K::Call: {
                if (e->args[0]->k != Expr::K::Var) {
                    diag(e->line, "字节码/汇编只支持按名字调用");
                    break;
                }
                for (size_t i = 1; i < e->args.size(); ++i) expr(e->args[i]);
                string name = e->args[0]->text;
                emit("call", "\"name\":" + quoteText(name) + ",\"n\":" +
                                  std::to_string(e->args.size() - 1),
                     "CALL " + name + " " + std::to_string(e->args.size() - 1));
                break;
            }
            case Expr::K::Index:
            case Expr::K::Slice:
            case Expr::K::List:
            case Expr::K::Dict:
                diag(e->line, "字节码/汇编暂不支持列表/字典/下标/切片");
                break;
        }
    }
    void stmt(const StmtPtr& s) {
        if (!s) return;
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k != Expr::K::Var) {
                    diag(s->line, "字节码/汇编只支持变量赋值");
                    break;
                }
                expr(s->expr);
                emit("store", "\"name\":" + quoteText(s->target->text), "STORE " + s->target->text);
                break;
            case Stmt::K::Print: {
                for (const auto& a : s->args) expr(a);
                emit("print", "\"n\":" + std::to_string(s->args.size()), "PRINT " + std::to_string(s->args.size()));
                break;
            }
            case Stmt::K::ExprStmt:
                expr(s->expr);
                emit("pop", "", "POP");
                break;
            case Stmt::K::If: {
                vector<int> ends;
                for (const auto& br : s->branches) {
                    expr(br.first);
                    int jz = emit("jz");
                    for (const auto& x : br.second) stmt(x);
                    ends.push_back(emit("jmp"));
                    patch(jz, (int)code.size());
                }
                for (const auto& x : s->elseBody) stmt(x);
                int end = (int)code.size();
                for (int j : ends) patch(j, end);
                break;
            }
            case Stmt::K::While: {
                int start = (int)code.size();
                expr(s->expr);
                int jz = emit("jz");
                for (const auto& x : s->body) stmt(x);
                int jmp = emit("jmp");
                patch(jmp, start);
                patch(jz, (int)code.size());
                break;
            }
            case Stmt::K::Break:
            case Stmt::K::Continue:
                diag(s->line, "字节码/汇编暂不支持中断/继续");
                break;
            case Stmt::K::Func:
                diag(s->line, "字节码/汇编暂不支持函数定义");
                break;
            case Stmt::K::For:
                diag(s->line, "字节码/汇编暂不支持对于循环");
                break;
            case Stmt::K::Try:
                diag(s->line, "字节码/汇编暂不支持尝试/捕获");
                break;
            case Stmt::K::Import:
                diag(s->line, "「依赖」不翻译");
                break;
            case Stmt::K::Export:
                break;
            case Stmt::K::Return:
                diag(s->line, "字节码/汇编暂不支持返回");
                break;
        }
    }
    string bytecode() {
        for (const auto& s : program) stmt(s);
        emit("halt", "", "HALT");
        std::ostringstream o;
        o << "{\"target\":\"bytecode\",\"code\":[";
        for (size_t i = 0; i < code.size(); ++i) {
            if (i) o << ",";
            o << "{\"op\":" << quoteText(code[i].op);
            if (!code[i].jsonArg.empty()) o << "," << code[i].jsonArg;
            if (code[i].target >= 0) o << ",\"to\":" << code[i].target;
            o << "}";
        }
        o << "]}";
        return o.str();
    }
    string assembly() {
        for (const auto& s : program) stmt(s);
        emit("halt", "", "HALT");
        std::ostringstream o;
        o << "; 翻译目标：汇编（自定义栈指令）\n";
        for (size_t i = 0; i < code.size(); ++i) {
            o << "L" << i << ":\n";
            const auto& c = code[i];
            if (c.op == "jz") o << "  JZ L" << c.target << "\n";
            else if (c.op == "jmp") o << "  JMP L" << c.target << "\n";
            else if (!c.asmArg.empty()) o << "  " << c.asmArg << "\n";
            else o << "  " << c.op << "\n";
        }
        return o.str();
    }
};

static string translateByTarget(const string& target, const vector<StmtPtr>& program,
                                vector<string>& diags) {
    if (target == "字节码") {
        StackEmitter em(program);
        string s = em.bytecode();
        for (const auto& d : em.diags) diags.push_back(d);
        return s;
    }
    if (target == "汇编") {
        StackEmitter em(program);
        string s = em.assembly();
        for (const auto& d : em.diags) diags.push_back(d);
        return s;
    }
    if (target == "C++") return translateToCppSource(program, diags);
    diags.push_back("第 1 行：未知翻译目标「" + target + "」");
    return "";
}

static vector<string> disassembleBytecode(const string& path, int line);
static vector<string> binaryStrings(const string& bytes, size_t minLen, size_t limit,
                                    const string& filter, int line);
static Value binaryHeader(const string& path, int line);
static Value binaryStringsValue(const string& path, int line, size_t minLen, size_t limit,
                                const string& filter);
static std::pair<int, string> runCapture(const string& cmd) {
    FILE* p = _popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return {-1, "无法启动命令"};
    string out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), p)) out += buf;
    int rc = _pclose(p);
    return {rc, out};
}

static Value compatParse(const string& lang0, const string& code, int line) {
    string lang = lowerAscii(lang0);
    if (lang == "json") {
        try {
            JsonReader reader(code, line);
            reader.parse();
            auto d = std::make_shared<Dict>();
            d->items["语言"] = vStr("json");
            d->items["ok"] = vBool(true);
            d->items["退出码"] = vNum(0);
            d->items["输出"] = vStr("");
            return vDict(d);
        } catch (WenError& e) {
            auto d = std::make_shared<Dict>();
            d->items["语言"] = vStr("json");
            d->items["ok"] = vBool(false);
            d->items["退出码"] = vNum(1);
            d->items["输出"] = vStr(e.what());
            return vDict(d);
        }
    }
    string ext, cmd;
    if (lang == "python") { ext = ".py"; cmd = "python -m py_compile"; }
    else if (lang == "javascript" || lang == "js") { ext = ".js"; cmd = "node --check"; }
    else if (lang == "typescript" || lang == "ts") { ext = ".ts"; cmd = "tsc --noEmit"; }
    else if (lang == "java") { ext = ".java"; cmd = "javac -d"; }
    else if (lang == "c") { ext = ".c"; cmd = "gcc -fsyntax-only"; }
    else if (lang == "cpp" || lang == "c++") { ext = ".cpp"; cmd = "g++ -fsyntax-only"; }
    else if (lang == "lua") { ext = ".lua"; cmd = "luac -p"; }
    else if (lang == "ruby") { ext = ".rb"; cmd = "ruby -c"; }
    else if (lang == "php") { ext = ".php"; cmd = "php -l"; }
    else if (lang == "bash" || lang == "shell") { ext = ".sh"; cmd = "bash -n"; }
    else if (lang == "rust") { ext = ".rs"; cmd = "rustc --emit=metadata --crate-type lib"; }
    else if (lang == "go") { ext = ".go"; cmd = "gofmt -e"; }
    else throw WenError("运行时错误", line, 0, "不支持的解析语言：" + lang0);
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmp = fs::temp_directory_path() /
                   fs::u8path("wen_parse_" + std::to_string((long long)stamp) + ext);
    try {
        writeWholeFile(tmp.u8string(), code, false);
        string full;
        if (lang == "java") {
            fs::path dir = tmp.parent_path();
            full = cmd + " \"" + dir.u8string() + "\" \"" + tmp.u8string() + "\"";
        } else {
            full = cmd + " \"" + tmp.u8string() + "\"";
        }
        auto [rc, out] = runCapture(full);
        auto d = std::make_shared<Dict>();
        d->items["语言"] = vStr(lang);
        d->items["ok"] = vBool(rc == 0);
        d->items["退出码"] = vNum((double)rc);
        d->items["输出"] = vStr(out);
        std::error_code ec;
        fs::remove(tmp, ec);
        return vDict(d);
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}
Interpreter::Interpreter() {
    globals = std::make_shared<Scope>();
    rng.seed(std::random_device{}());

    // ---------- 核心（不需要依赖）---------
    registerBuiltin("长度", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        const Value& v = a[0];
        if (v.type == VT::Str) return vNum((double)utf8Length(v.str));
        if (v.type == VT::List && v.list) return vNum((double)v.list->items.size());
        if (v.type == VT::Set && v.list) return vNum((double)v.list->items.size());
        if (v.type == VT::Dict && v.dict) return vNum((double)v.dict->items.size());
        if (v.type == VT::Bytes && v.bytes) return vNum((double)v.bytes->size());
        if (v.type == VT::Range) {
            double span = (v.rstop - v.rstart) / v.rstep;
            return vNum(span <= 0 ? 0.0 : std::ceil(span - 1e-12));
        }
        throw WenError("运行时错误", line, 0, "「长度」不支持" + typeName(v));
    });
    registerBuiltin("类型", 1, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        return vStr(typeName(a[0]));
    });
    registerBuiltin("文本", 1, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        return vStr(toText(a[0], false));
    });
    registerBuiltin("查找", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "查找", line);
        string sub = needStr(a[1], "查找", line);
        long long from = a.size() == 3 ? needIndex(a[2], "查找", line) : 0;
        if (from < 0) from = 0;
        if ((size_t)from > s.size()) return vNum(-1);
        size_t pos = s.find(sub, (size_t)from);
        return vNum(pos == string::npos ? -1.0 : (double)pos);
    });
    registerBuiltin("替换", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "替换", line);
        string from = needStr(a[1], "替换", line);
        string to = needStr(a[2], "替换", line);
        if (from.empty()) return vStr(s);
        string out;
        size_t pos = 0;
        while (true) {
            size_t hit = s.find(from, pos);
            if (hit == string::npos) {
                out += s.substr(pos);
                break;
            }
            out += s.substr(pos, hit - pos);
            out += to;
            pos = hit + from.size();
        }
        return vStr(out);
    });
    registerBuiltin("分割", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "分割", line);
        string sep = needStr(a[1], "分割", line);
        auto l = std::make_shared<List>();
        if (sep.empty()) {
            for (const auto& ch : utf8Chars(s)) l->items.push_back(vStr(ch));
            return vList(l);
        }
        size_t pos = 0;
        while (true) {
            size_t hit = s.find(sep, pos);
            if (hit == string::npos) {
                l->items.push_back(vStr(s.substr(pos)));
                break;
            }
            l->items.push_back(vStr(s.substr(pos, hit - pos)));
            pos = hit + sep.size();
        }
        return vList(l);
    });
    registerBuiltin("修剪", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "修剪", line);
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return vStr(s.substr(b, e - b));
    });
    registerBuiltin("整数", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(std::trunc(needNum(a[0], "整数", line)));
    });
    registerBuiltin("小数", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "小数", line));
    });
    registerBuiltin("布尔", 1, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        return vBool(truthy(a[0]));
    });

    registerBuiltin("范围", 1, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double start = 0, stop = 0, step = 1;
        if (a.size() == 1) {
            stop = needNum(a[0], "范围", line);
        } else {
            start = needNum(a[0], "范围", line);
            stop = needNum(a[1], "范围", line);
            if (a.size() == 3) step = needNum(a[2], "范围", line);
        }
        if (step == 0) throw WenError("运行时错误", line, 0, "范围的步长不能为 0");
        double span = (stop - start) / step;
        if (span <= 0) return vList();
        long long n = (long long)std::ceil(span - 1e-12);
        if (n > 1000000)
            throw WenError("运行时错误", line, 0, "范围太大（超过一百万个元素）");
        auto l = std::make_shared<List>();
        for (long long i = 0; i < n; ++i) l->items.push_back(vNum(start + i * step));
        return vList(l);
    });
    registerBuiltin("范围流", 1, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double start = 0, stop = 0, step = 1;
        if (a.size() == 1) {
            stop = needNum(a[0], "范围流", line);
        } else {
            start = needNum(a[0], "范围流", line);
            stop = needNum(a[1], "范围流", line);
            if (a.size() == 3) step = needNum(a[2], "范围流", line);
        }
        if (step == 0) throw WenError("运行时错误", line, 0, "范围流的步长不能为 0");
        return vRange(start, stop, step);
    });

    registerBuiltin("追加", 1, -1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "追加", line);
        for (size_t i = 1; i < a.size(); ++i) l->items.push_back(a[i]);
        return a[0];
    });
    registerBuiltin("弹出", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "弹出", line);
        if (l->items.empty())
            throw WenError("运行时错误", line, 0, "不能从空列表里弹出");
        long long idx = (long long)l->items.size() - 1;
        if (a.size() == 2) {
            idx = needIndex(a[1], "弹出", line);
            long long n = (long long)l->items.size();
            if (idx < 0) idx += n;
            if (idx < 0 || idx >= n)
                throw WenError("运行时错误", line, 0, "弹出位置越界");
        }
        Value v = l->items[(size_t)idx];
        l->items.erase(l->items.begin() + idx);
        return v;
    });
    registerBuiltin("插入", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "插入", line);
        long long idx = needIndex(a[1], "插入", line);
        long long n = (long long)l->items.size();
        if (idx < 0) idx += n + 1;
        if (idx < 0 || idx > n)
            throw WenError("运行时错误", line, 0, "插入位置越界");
        l->items.insert(l->items.begin() + idx, a[2]);
        return a[0];
    });
    registerBuiltin("排序", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "排序", line);
        bool reverse = a.size() == 2 && truthy(a[1]);
        vector<Value> items = l->items;
        bool allNum = true, allStr = true;
        for (const auto& v : items) {
            if (v.type != VT::Num) allNum = false;
            if (v.type != VT::Str) allStr = false;
        }
        if (!allNum && !allStr)
            throw WenError("运行时错误", line, 0, "排序只支持全是数字或全是文本的列表");
        std::sort(items.begin(), items.end(), [&](const Value& x, const Value& y) {
            return allNum ? x.num < y.num : x.str < y.str;
        });
        if (reverse) std::reverse(items.begin(), items.end());
        auto out = std::make_shared<List>();
        out->items = std::move(items);
        return vList(out);
    });
    registerBuiltin("反转", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "反转", line);
        auto out = std::make_shared<List>();
        out->items = l->items;
        std::reverse(out->items.begin(), out->items.end());
        return vList(out);
    });
    registerBuiltin("键", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto d = needDict(a[0], "键", line);
        auto l = std::make_shared<List>();
        for (const auto& kv : d->items) l->items.push_back(vStr(kv.first));
        return vList(l);
    });
    registerBuiltin("值", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto d = needDict(a[0], "值", line);
        auto l = std::make_shared<List>();
        for (const auto& kv : d->items) l->items.push_back(kv.second);
        return vList(l);
    });
    registerBuiltin("有键", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto d = needDict(a[0], "有键", line);
        string k = needStr(a[1], "有键", line);
        return vBool(d->items.count(k) > 0);
    });

    registerBuiltin("输入", 0, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        if (!a.empty()) {
            std::cout << toText(a[0]);
            std::cout.flush();
        }
        string s;
        if (!std::getline(std::cin, s)) return vNone();
        stripBom(s);
        if (!s.empty() && s.back() == '\r') s.pop_back();
        return vStr(s);
    });
    registerBuiltin("读整数", 0, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        while (true) {
            if (!a.empty()) {
                std::cout << toText(a[0]);
                std::cout.flush();
            }
            string s;
            if (!std::getline(std::cin, s)) return vNone();
            stripBom(s);
            if (!s.empty() && s.back() == '\r') s.pop_back();
            double d = 0;
            if (parseNumberText(s, d) && std::floor(d) == d) return vNum(d);
            std::cout << "请输入整数。\n";
        }
    });
    registerBuiltin("读小数", 0, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        while (true) {
            if (!a.empty()) {
                std::cout << toText(a[0]);
                std::cout.flush();
            }
            string s;
            if (!std::getline(std::cin, s)) return vNone();
            stripBom(s);
            if (!s.empty() && s.back() == '\r') s.pop_back();
            double d = 0;
            if (parseNumberText(s, d)) return vNum(d);
            std::cout << "请输入数字。\n";
        }
    });
    registerBuiltin("确认", 0, 1, [](Interpreter&, vector<Value>& a, int) -> Value {
        while (true) {
            if (!a.empty()) {
                std::cout << toText(a[0]);
                std::cout.flush();
            }
            string s;
            if (!std::getline(std::cin, s)) return vBool(false);
            stripBom(s);
            string t = trim(s);
            if (t == "是" || t == "y" || t == "Y" || t == "yes" || t == "对" || t == "1")
                return vBool(true);
            if (t == "否" || t == "n" || t == "N" || t == "no" || t == "不" || t == "0")
                return vBool(false);
            std::cout << "请输入 是 或 否。\n";
        }
    });
    registerBuiltin("抛出", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        throw WenError("运行时错误", line, 0, toText(a[0]));
    });

    registerBuiltin("读取", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "读取", line);
        try {
            return vStr(readWholeFile(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
    });
    registerBuiltin("运行", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "运行", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        vector<WenError> saveErrors;
        string expanded;
        processSaveBlocks(src, true, expanded, saveErrors);
        if (!saveErrors.empty()) throw saveErrors[0];
        src = expanded;
        string oldFile = vm.filename;
        vector<string> oldLines = vm.sourceLines;
        vm.filename = path;
        vm.sourceLines = splitLines(src);
        try {
            auto program = vm.parseSource(src);
            vm.execProgram(program);
        } catch (IncompleteError& e) {
            vm.filename = oldFile;
            vm.sourceLines = oldLines;
            throw WenError("语法错误", e.line, 0, e.what());
        } catch (...) {
            vm.filename = oldFile;
            vm.sourceLines = oldLines;
            throw;
        }
        vm.filename = oldFile;
        vm.sourceLines = oldLines;
        return vNone();
    });

    registerBuiltin("调用外部", 2, -1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "调用外部", line);
        string name = needStr(a[1], "调用外部", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        // 外部算法在独立解释器里跑：报错、变量都隔离在主程序之外。
        Interpreter sub;
        try {
            sub.inLibrary_ = true;
            sub.run(src, path);
            Value fn;
            if (!sub.globals->lookup(name, fn))
                throw WenError("运行时错误", line, 0,
                               "外部库「" + path + "」里没有「" + name + "」");
            vector<Value> args(a.begin() + 2, a.end());
            return sub.callValue(fn, args, line);
        } catch (WenError& e) {
            throw WenError("运行时错误", line, 0,
                           "外部库「" + path + "」第 " + std::to_string(e.line) +
                               " 行的「" + name + "」失败：" + e.what());
        } catch (IncompleteError& e) {
            throw WenError("语法错误", line, 0,
                           "外部库「" + path + "」不完整：" + e.what());
        }
    });

    // ---------- 翻译器 ----------
    registerBuiltin("翻译支持", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        l->items.push_back(vStr("字节码"));
        l->items.push_back(vStr("C++"));
        l->items.push_back(vStr("汇编"));
        return vList(l);
    });
    registerBuiltin("翻译目标", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "翻译目标", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        auto ts = translationTargets(src);
        if (ts.size() != 1) return vNone();
        return vStr(ts[0].name);
    });
    registerBuiltin("翻译检查", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "翻译检查", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        vector<string> diags;
        auto ts = translationTargets(src);
        if (ts.empty()) {
            diags.push_back("第 1 行：缺少翻译目标；第一行写「# 翻译: 字节码 / C++ / 汇编」");
        } else if (ts.size() > 1) {
            for (const auto& t : ts)
                diags.push_back("第 " + std::to_string(t.line) + " 行：翻译目标「" + t.name + "」");
            diags.push_back("冲突：一个文件只能有一个翻译目标，不能共用；请删掉多余的目标行");
        } else {
            string target = ts[0].name;
            bool ok = target == "字节码" || target == "C++" || target == "汇编";
            if (!ok) {
                diags.push_back("第 " + std::to_string(ts[0].line) + " 行：未知翻译目标「" +
                                target + "」；只支持 字节码 / C++ / 汇编");
            } else {
                try {
                    Interpreter sub;
                    sub.filename = path;
                    sub.sourceLines = splitLines(src);
                    auto program = sub.parseSource(src);
                    vector<string> d;
                    translateByTarget(target, program, d);
                    for (const auto& x : d) diags.push_back(x);
                } catch (WenError& e) {
                    diags.push_back("第 " + std::to_string(e.line) + " 行：语法错误：" + e.what());
                } catch (IncompleteError& e) {
                    diags.push_back("第 " + std::to_string(e.line) + " 行：代码不完整：" + e.what());
                }
            }
        }
        auto out = std::make_shared<List>();
        for (auto d : diags) out->items.push_back(vStr("文件「" + path + "」" + d));
        return vList(out);
    });
    registerBuiltin("翻译", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "翻译", line);
        string outPath = needStr(a[1], "翻译", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        vector<string> diags;
        auto ts = translationTargets(src);
        string target;
        string code;
        if (ts.empty()) {
            diags.push_back("第 1 行：缺少翻译目标；第一行写「# 翻译: 字节码 / C++ / 汇编」");
        } else if (ts.size() > 1) {
            for (const auto& t : ts)
                diags.push_back("第 " + std::to_string(t.line) + " 行：翻译目标「" + t.name + "」");
            diags.push_back("冲突：一个文件只能有一个翻译目标，不能共用；请删掉多余的目标行");
        } else {
            target = ts[0].name;
            bool ok = target == "字节码" || target == "C++" || target == "汇编";
            if (!ok) {
                diags.push_back("第 " + std::to_string(ts[0].line) + " 行：未知翻译目标「" +
                                target + "」；只支持 字节码 / C++ / 汇编");
            } else {
                try {
                    Interpreter sub;
                    sub.filename = path;
                    sub.sourceLines = splitLines(src);
                    auto program = sub.parseSource(src);
                    code = translateByTarget(target, program, diags);
                } catch (WenError& e) {
                    diags.push_back("第 " + std::to_string(e.line) + " 行：语法错误：" + e.what());
                } catch (IncompleteError& e) {
                    diags.push_back("第 " + std::to_string(e.line) + " 行：代码不完整：" + e.what());
                }
            }
        }
        if (!diags.empty()) {
            std::ostringstream rep;
            rep << "翻译目标：" << (target.empty() ? "未声明" : target) << "\n";
            rep << "文件：" << path << "\n";
            rep << "问题数：" << diags.size() << "\n\n";
            for (const auto& d : diags)
                rep << "- 文件「" << path << "」" << d << "\n";
            rep << "\n建议：一个文件只留一个目标行，再重新翻译。\n";
            string report = outPath + ".翻译报告.txt";
            try {
                ensureParentDirectory(report);
                writeWholeFile(report, rep.str(), false);
            } catch (IoError& e) {
                throw WenError("文件错误", line, 0, e.what());
            }
            throw WenError("翻译错误", line, 0,
                           "翻译失败：" + std::to_string(diags.size()) +
                               " 个问题；报告：" + report);
        }
        try {
            ensureParentDirectory(outPath);
            writeWholeFile(outPath, code, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(outPath);
    });

    registerBuiltin("兼容语言", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        for (const char* s : {"python", "javascript", "typescript", "json", "java",
                              "c", "cpp", "lua", "ruby", "php", "bash", "rust", "go"})
            l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("兼容解析", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return compatParse(needStr(a[0], "兼容解析", line),
                           needStr(a[1], "兼容解析", line), line);
    });
    registerBuiltin("兼容支持", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        for (const char* s : {"python", "javascript", "json", "java", "字节码", "C++", "汇编"})
            l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("兼容翻译", 3, 3, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string srcPath = needStr(a[0], "兼容翻译", line);
        string target = needStr(a[1], "兼容翻译", line);
        string outPath = needStr(a[2], "兼容翻译", line);
        string t = lowerAscii(target);
        if (t == "js") t = "javascript";
        if (t == "cpp") t = "C++";
        bool external = t == "python" || t == "javascript" || t == "json" || t == "java";
        bool internal = t == "字节码" || t == "C++" || t == "汇编";
        if (!external && !internal)
            throw WenError("运行时错误", line, 0, "不支持的兼容目标：" + target);
        string src;
        try {
            src = readWholeFile(resolveWenPath(srcPath));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        std::ostringstream body;
        for (const auto& raw : splitLines(src)) {
            size_t b = raw.find_first_not_of(" \t");
            if (b == string::npos) { body << raw << "\n"; continue; }
            string head = raw.substr(b);
            if (head.rfind("# 翻译:", 0) == 0 || head.rfind("# 外派:", 0) == 0) continue;
            body << raw << "\n";
        }
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmp = fs::temp_directory_path() /
                       fs::u8path("wen_compat_" + std::to_string((long long)stamp) + ".wen");
        string text = (external ? "# 外派: " : "# 翻译: ") + t + "\n" + body.str();
        try {
            writeWholeFile(tmp.u8string(), text, false);
            Value fn;
            string bridge = external ? "外派翻译" : "翻译";
            if (!globals->lookup(bridge, fn))
                throw WenError("运行时错误", line, 0, "缺少翻译入口：" + bridge);
            vector<Value> args{vStr(tmp.u8string()), vStr(outPath)};
            Value result = callValue(fn, args, line);
            fs::remove(tmp);
            return result;
        } catch (...) {
            std::error_code ec;
            fs::remove(tmp, ec);
            throw;
        }
    });
    registerBuiltin("兼容运行", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string src = needStr(a[0], "兼容运行", line);
        string target = lowerAscii(needStr(a[1], "兼容运行", line));
        if (target == "js") target = "javascript";
        if (target != "python" && target != "字节码")
            throw WenError("运行时错误", line, 0,
                           "兼容运行目前只支持 python / 字节码；其他目标需先装对应运行时");
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        string ext = target == "python" ? ".py" : ".json";
        fs::path out = fs::temp_directory_path() /
                       fs::u8path("wen_compat_run_" + std::to_string((long long)stamp) + ext);
        Value trans;
        if (!globals->lookup("兼容翻译", trans))
            throw WenError("运行时错误", line, 0, "缺少兼容翻译");
        vector<Value> targs{vStr(src), vStr(target), vStr(out.u8string())};
        callValue(trans, targs, line);
        int rc = 0;
        if (target == "python") {
            string cmd = "python \"" + out.u8string() + "\"";
            rc = std::system(cmd.c_str());
        } else {
            Value exec;
            if (!globals->lookup("执行字节码", exec)) {
                std::error_code ec;
                fs::remove(out, ec);
                throw WenError("运行时错误", line, 0, "缺少执行字节码");
            }
            vector<Value> eargs{vStr(out.u8string())};
            callValue(exec, eargs, line);
        }
        std::error_code ec;
        fs::remove(out, ec);
        return vNum((double)rc);
    });
    registerBuiltin("执行字节码", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "执行字节码", line);
        string text;
        try {
            text = readWholeFile(path);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        JsonReader reader(text, line);
        Value root = reader.parse();
        auto rootDict = needDict(root, "字节码", line);
        auto codeIt = rootDict->items.find("code");
        if (codeIt == rootDict->items.end())
            throw WenError("运行时错误", line, 0, "字节码缺少 code");
        auto code = needList(codeIt->second, "字节码", line);
        vector<Value> stack;
        std::map<string, Value> vars;
        auto pop = [&]() -> Value {
            if (stack.empty())
                throw WenError("运行时错误", line, 0, "字节码栈为空");
            Value v = stack.back();
            stack.pop_back();
            return v;
        };
        size_t pc = 0;
        while (pc < code->items.size()) {
            auto ins = needDict(code->items[pc], "字节码指令", line);
            string op = needStr(ins->items.at("op"), "字节码 op", line);
            auto arg = [&](const string& k) -> Value {
                auto it = ins->items.find(k);
                if (it == ins->items.end())
                    throw WenError("运行时错误", line, 0, "字节码指令缺少 " + k);
                return it->second;
            };
            if (op == "halt") break;
            if (op == "push") {
                stack.push_back(arg("v"));
            } else if (op == "pop") {
                pop();
            } else if (op == "load") {
                string name = needStr(arg("name"), "字节码 load", line);
                auto it = vars.find(name);
                if (it == vars.end())
                    throw WenError("运行时错误", line, 0, "字节码变量不存在：" + name);
                stack.push_back(it->second);
            } else if (op == "store") {
                string name = needStr(arg("name"), "字节码 store", line);
                vars[name] = pop();
            } else if (op == "neg") {
                stack.push_back(vNum(-needNum(pop(), "字节码 neg", line)));
            } else if (op == "not") {
                stack.push_back(vBool(!truthy(pop())));
            } else if (op == "add" || op == "sub" || op == "mul" || op == "div" ||
                       op == "idiv" || op == "mod" || op == "pow") {
                Value b = pop(), aa = pop();
                double x = needNum(aa, "字节码运算", line), y = needNum(b, "字节码运算", line);
                if (op == "add") stack.push_back(vNum(x + y));
                else if (op == "sub") stack.push_back(vNum(x - y));
                else if (op == "mul") stack.push_back(vNum(x * y));
                else if (op == "div") stack.push_back(vNum(x / y));
                else if (op == "idiv") stack.push_back(vNum(std::floor(x / y)));
                else if (op == "mod") stack.push_back(vNum(std::fmod(x, y)));
                else stack.push_back(vNum(std::pow(x, y)));
            } else if (op == "eq" || op == "ne" || op == "lt" || op == "le" ||
                       op == "gt" || op == "ge") {
                Value b = pop(), aa = pop();
                double x = needNum(aa, "字节码比较", line), y = needNum(b, "字节码比较", line);
                if (op == "eq") stack.push_back(vBool(x == y));
                else if (op == "ne") stack.push_back(vBool(x != y));
                else if (op == "lt") stack.push_back(vBool(x < y));
                else if (op == "le") stack.push_back(vBool(x <= y));
                else if (op == "gt") stack.push_back(vBool(x > y));
                else stack.push_back(vBool(x >= y));
            } else if (op == "and" || op == "or") {
                Value b = pop(), aa = pop();
                if (op == "and") stack.push_back(vBool(truthy(aa) && truthy(b)));
                else stack.push_back(vBool(truthy(aa) || truthy(b)));
            } else if (op == "call") {
                string fname = needStr(arg("name"), "字节码 call", line);
                int n = (int)needIndex(arg("n"), "字节码 call", line);
                vector<Value> args((size_t)n);
                for (int i = n - 1; i >= 0; --i) args[(size_t)i] = pop();
                Value fn;
                if (!globals->lookup(fname, fn))
                    throw WenError("运行时错误", line, 0, "字节码调用不存在的名字：" + fname);
                stack.push_back(callValue(fn, args, line));
            } else if (op == "print") {
                int n = (int)needIndex(arg("n"), "字节码 print", line);
                vector<Value> vals;
                for (int i = 0; i < n; ++i) vals.push_back(pop());
                for (int i = n - 1; i >= 0; --i) {
                    if (i != n - 1) std::cout << " ";
                    std::cout << toText(vals[(size_t)i]);
                }
                std::cout << "\n";
            } else if (op == "jz") {
                Value cond = pop();
                if (!truthy(cond)) {
                    pc = (size_t)needIndex(arg("to"), "字节码 jz", line);
                    continue;
                }
            } else if (op == "jmp") {
                pc = (size_t)needIndex(arg("to"), "字节码 jmp", line);
                continue;
            } else {
                throw WenError("运行时错误", line, 0, "字节码未知指令：" + op);
            }
            ++pc;
        }
        return vNone();
    });
    registerBuiltin("反汇编", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto lines = disassembleBytecode(needStr(a[0], "反汇编", line), line);
        auto l = std::make_shared<List>();
        for (const auto& s : lines) l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("反汇编文本", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto lines = disassembleBytecode(needStr(a[0], "反汇编文本", line), line);
        string text;
        for (const auto& s : lines) text += s + "\n";
        if (a.size() == 1) return vStr(text);
        string path = needStr(a[1], "反汇编文本", line);
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, text, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });
    registerBuiltin("二进制头", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return binaryHeader(needStr(a[0], "二进制头", line), line);
    });
    registerBuiltin("二进制字符串", 1, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long minLen = a.size() > 1 ? needIndex(a[1], "二进制字符串", line) : 8;
        if (minLen < 1) minLen = 1;
        if (minLen > 256) minLen = 256;
        string filter = a.size() > 2 ? needStr(a[2], "二进制字符串", line) : "";
        return binaryStringsValue(needStr(a[0], "二进制字符串", line), line,
                                  (size_t)minLen, 200, filter);
    });
    registerBuiltin("二进制反汇编", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "二进制反汇编", line);
        auto d = needDict(binaryHeader(path, line), "二进制反汇编", line);
        string filter = a.size() > 1 ? needStr(a[1], "二进制反汇编", line) : "";
        d->items["字符串"] = binaryStringsValue(path, line, 8, 200, filter);
        return vDict(d);
    });
    registerBuiltin("反二进制", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "反二进制", line);
        auto d = needDict(binaryHeader(path, line), "反二进制", line);
        string filter = a.size() > 1 ? needStr(a[1], "反二进制", line) : "";
        d->items["字符串"] = binaryStringsValue(path, line, 8, 200, filter);
        return vDict(d);
    });
    registerBuiltin("暴力还原", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string filter = a.size() > 1 ? needStr(a[1], "暴力还原", line) : "";
        return binaryStringsValue(needStr(a[0], "暴力还原", line), line, 8, 200, filter);
    });
    // ---------- 外派库 ----------
    registerBuiltin("外派支持", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        l->items.push_back(vStr("python"));
        l->items.push_back(vStr("javascript"));
        l->items.push_back(vStr("json"));
        l->items.push_back(vStr("java"));
        return vList(l);
    });
    registerBuiltin("外派目标", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "外派目标", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        string target = externalTarget(src);
        return target.empty() ? vNone() : vStr(target);
    });
    registerBuiltin("外派检查", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "外派检查", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        vector<string> diags;
        string target = externalTarget(src);
        bool ok = target == "python" || target == "javascript" || target == "js" ||
                  target == "json" || target == "gson" || target == "java";
        if (target.empty())
            diags.push_back("第 1 行：缺少外派目标；第一行写「# 外派: python」");
        else if (!ok)
            diags.push_back("第 1 行：目标「" + target +
                            "」的适配器未安装；当前支持 python / javascript / json / java");
        if (ok) {
            try {
                Interpreter sub;
                sub.filename = path;
                sub.sourceLines = splitLines(src);
                auto program = sub.parseSource(src);
                if (target == "python") {
                    PyEmitter em(program);
                    em.generate();
                    for (const auto& d : em.diags) diags.push_back(d);
                } else if (target == "json" || target == "gson") {
                    JsonEmitter em(program);
                    em.generate();
                } else if (target == "java") {
                    JavaEmitter em(program);
                    em.generate();
                    for (const auto& d : em.diags) diags.push_back(d);
                } else {
                    JsEmitter em(program);
                    em.generate();
                    for (const auto& d : em.diags) diags.push_back(d);
                }
            } catch (WenError& e) {
                diags.push_back("第 " + std::to_string(e.line) + " 行：语法错误：" + e.what());
            } catch (IncompleteError& e) {
                diags.push_back("第 " + std::to_string(e.line) + " 行：代码不完整：" + e.what());
            }
        }
        auto out = std::make_shared<List>();
        for (auto d : diags) {
            d = "文件「" + path + "」" + d;
            out->items.push_back(vStr(d));
        }
        return vList(out);
    });
    registerBuiltin("外派翻译", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "外派翻译", line);
        string outPath = needStr(a[1], "外派翻译", line);
        string src;
        try {
            src = readWholeFile(resolveWenPath(path));
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        vector<string> diags;
        string target = externalTarget(src);
        bool ok = target == "python" || target == "javascript" || target == "js" ||
                  target == "json" || target == "gson" || target == "java";
        if (target.empty())
            diags.push_back("第 1 行：缺少外派目标；第一行写「# 外派: python」");
        else if (!ok)
            diags.push_back("第 1 行：目标「" + target +
                            "」的适配器未安装；当前支持 python / javascript / json / java");
        string code;
        if (ok) {
            try {
                Interpreter sub;
                sub.filename = path;
                sub.sourceLines = splitLines(src);
                auto program = sub.parseSource(src);
                if (target == "python") {
                    PyEmitter em(program);
                    code = em.generate();
                    for (const auto& d : em.diags) diags.push_back(d);
                } else if (target == "json" || target == "gson") {
                    JsonEmitter em(program);
                    code = em.generate();
                } else if (target == "java") {
                    JavaEmitter em(program);
                    code = em.generate();
                    for (const auto& d : em.diags) diags.push_back(d);
                } else {
                    JsEmitter em(program);
                    code = em.generate();
                    for (const auto& d : em.diags) diags.push_back(d);
                }
            } catch (WenError& e) {
                diags.push_back("第 " + std::to_string(e.line) + " 行：语法错误：" + e.what());
            } catch (IncompleteError& e) {
                diags.push_back("第 " + std::to_string(e.line) + " 行：代码不完整：" + e.what());
            }
        }
        if (!diags.empty()) {
            std::ostringstream rep;
            rep << "外派目标：" << (target.empty() ? "未声明" : target) << "\n";
            rep << "文件：" << path << "\n";
            rep << "问题数：" << diags.size() << "\n\n";
            for (const auto& d : diags)
                rep << "- 文件「" << path << "」" << d << "\n";
            rep << "\n建议：先修完上面的问题，再重新外派。\n";
            string report = outPath + ".外派报告.txt";
            try {
                ensureParentDirectory(report);
                writeWholeFile(report, rep.str(), false);
            } catch (IoError& e) {
                throw WenError("文件错误", line, 0, e.what());
            }
            throw WenError("外派错误", line, 0,
                           "外派失败：" + std::to_string(diags.size()) +
                               " 个问题；报告：" + report);
        }
        try {
            ensureParentDirectory(outPath);
            writeWholeFile(outPath, code, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(outPath);
    });

    // ---------- 内存库 ----------
    registerBuiltin("内存存", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "内存存", line);
        if (memoryCompress && isPulled(key)) {
            string raw = jsonDump(a[1], line);
            buildMemoryDict(raw);
            memoryOrigSize[key] = raw.size();
            memoryCompressed[key] = compressMemoryText(raw);
        } else {
            memoryStore[key] = a[1];
        }
        return a[1];
    });
    registerBuiltin("内存取", 1, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "内存取", line);
        auto it = memoryStore.find(key);
        if (it != memoryStore.end()) {
            ++memoryHits;
            return it->second;
        }
        auto tit = memoryTemp.find(key);
        if (tit != memoryTemp.end()) {
            ++memoryHits;
            return tit->second;
        }
        auto cit = memoryCompressed.find(key);
        if (cit != memoryCompressed.end()) {
            string raw = decompressMemoryText(cit->second, line);
            JsonReader reader(raw, line);
            Value v = reader.parse();
            memoryStore[key] = v;
            memoryCompressed.erase(cit);
            memoryOrigSize.erase(key);
            ++memoryHits;
            return v;
        }
        ++memoryMisses;
        if (a.size() == 2) return a[1];
        throw WenError("运行时错误", line, 0, "内存里没有「" + key + "」");
    });
    registerBuiltin("内存有", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "内存有", line);
        return vBool(memoryStore.count(key) || memoryTemp.count(key) ||
                     memoryCompressed.count(key));
    });
    registerBuiltin("内存删", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "内存删", line);
        bool ok = memoryStore.erase(key) > 0;
        ok = memoryTemp.erase(key) > 0 || ok;
        ok = memoryCompressed.erase(key) > 0 || ok;
        return vBool(ok);
    });
    registerBuiltin("内存清空", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryStore.clear();
        memoryTemp.clear();
        memoryCompressed.clear();
        memoryOrigSize.clear();
        memoryDict.clear();
        memoryDictIndex.clear();
        memoryHits = 0;
        memoryMisses = 0;
        return vNone();
    });
    registerBuiltin("内存大小", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)(memoryStore.size() + memoryTemp.size() +
                             memoryCompressed.size()));
    });
    registerBuiltin("内存键", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        auto out = std::make_shared<List>();
        for (const auto& kv : memoryStore) out->items.push_back(vStr(kv.first));
        for (const auto& kv : memoryTemp) out->items.push_back(vStr(kv.first));
        for (const auto& kv : memoryCompressed) out->items.push_back(vStr(kv.first));
        return vList(out);
    });
    registerBuiltin("内存临时存", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "内存临时存", line);
        memoryTemp[key] = a[1];
        return a[1];
    });
    registerBuiltin("内存临时取", 1, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "内存临时取", line);
        auto it = memoryTemp.find(key);
        if (it != memoryTemp.end()) return it->second;
        if (a.size() == 2) return a[1];
        throw WenError("运行时错误", line, 0, "临时内存里没有「" + key + "」");
    });
    registerBuiltin("内存清临时", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryTemp.clear();
        return vNone();
    });
    registerBuiltin("内存命中", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)memoryHits);
    });
    registerBuiltin("开启内存", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryAuto = true;
        return vBool(true);
    });
    registerBuiltin("关闭内存", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryAuto = false;
        return vBool(true);
    });
    registerBuiltin("开启内存压缩", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryCompress = true;
        return vBool(true);
    });
    registerBuiltin("关闭内存压缩", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryCompress = false;
        return vBool(true);
    });
    registerBuiltin("压缩内存", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "压缩内存", line);
        auto it = memoryStore.find(key);
        if (it != memoryStore.end()) {
            string raw = jsonDump(it->second, line);
            buildMemoryDict(raw);
            memoryOrigSize[key] = raw.size();
            memoryCompressed[key] = compressMemoryText(raw);
            memoryStore.erase(it);
            return vBool(true);
        }
        auto tit = memoryTemp.find(key);
        if (tit != memoryTemp.end()) {
            string raw = jsonDump(tit->second, line);
            buildMemoryDict(raw);
            memoryOrigSize[key] = raw.size();
            memoryCompressed[key] = compressMemoryText(raw);
            memoryTemp.erase(tit);
            return vBool(true);
        }
        return vBool(false);
    });
    registerBuiltin("解压内存", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = needStr(a[0], "解压内存", line);
        auto it = memoryCompressed.find(key);
        if (it == memoryCompressed.end()) return vBool(false);
        string raw = decompressMemoryText(it->second, line);
        JsonReader reader(raw, line);
        memoryStore[key] = reader.parse();
        memoryCompressed.erase(it);
        memoryOrigSize.erase(key);
        return vBool(true);
    });
    registerBuiltin("压缩全部内存", 0, 0, [this](Interpreter&, vector<Value>&, int line) -> Value {
        buildMemoryDict();
        for (const auto& kv : memoryStore) {
            string raw = jsonDump(kv.second, line);
            memoryOrigSize[kv.first] = raw.size();
            memoryCompressed[kv.first] = compressMemoryText(raw);
        }
        for (const auto& kv : memoryTemp) {
            string raw = jsonDump(kv.second, line);
            memoryOrigSize[kv.first] = raw.size();
            memoryCompressed[kv.first] = compressMemoryText(raw);
        }
        memoryStore.clear();
        memoryTemp.clear();
        return vNone();
    });
    registerBuiltin("内存压缩大小", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)memoryCompressed.size());
    });
    registerBuiltin("内存复用率", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        long long total = memoryHits + memoryMisses;
        return vNum(total == 0 ? 0.0 : (double)memoryHits / (double)total);
    });
    registerBuiltin("牵引", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        auto add = [&](const string& item) {
            if (item.empty()) return;
            if (item == "全部" || item == "*") memoryPullAll = true;
            else if (item.back() == '*') memoryPullPrefixes.push_back(item.substr(0, item.size() - 1));
            else memoryPullKeys.insert(item);
        };
        if (a[0].type == VT::List && a[0].list) {
            for (const auto& v : a[0].list->items) add(needStr(v, "牵引", line));
        } else {
            add(needStr(a[0], "牵引", line));
        }
        return vBool(true);
    });
    registerBuiltin("牵引范围", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string prefix = needStr(a[0], "牵引范围", line);
        if (prefix == "全部" || prefix == "*") memoryPullAll = true;
        else memoryPullPrefixes.push_back(prefix);
        return vBool(true);
    });
    registerBuiltin("牵引名单", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        auto out = std::make_shared<List>();
        if (memoryPullAll) out->items.push_back(vStr("全部"));
        for (const auto& k : memoryPullKeys) out->items.push_back(vStr(k));
        for (const auto& p : memoryPullPrefixes) out->items.push_back(vStr(p + "*"));
        return vList(out);
    });
    registerBuiltin("牵引清空", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        memoryPullKeys.clear();
        memoryPullPrefixes.clear();
        memoryPullAll = false;
        return vNone();
    });
    registerBuiltin("压缩牵引", 0, 0, [this](Interpreter&, vector<Value>&, int line) -> Value {
        long long n = 0;
        buildMemoryDict();
        for (auto it = memoryStore.begin(); it != memoryStore.end();) {
            if (isPulled(it->first)) {
                string raw = jsonDump(it->second, line);
                memoryOrigSize[it->first] = raw.size();
                memoryCompressed[it->first] = compressMemoryText(raw);
                it = memoryStore.erase(it);
                ++n;
            } else {
                ++it;
            }
        }
        for (auto it = memoryTemp.begin(); it != memoryTemp.end();) {
            if (isPulled(it->first)) {
                string raw = jsonDump(it->second, line);
                memoryOrigSize[it->first] = raw.size();
                memoryCompressed[it->first] = compressMemoryText(raw);
                it = memoryTemp.erase(it);
                ++n;
            } else {
                ++it;
            }
        }
        return vNum((double)n);
    });
    registerBuiltin("内存压缩率", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        size_t orig = 0, comp = 0;
        for (const auto& kv : memoryOrigSize) orig += kv.second;
        for (const auto& kv : memoryCompressed) comp += kv.second.size();
        if (orig == 0) return vNum(0);
        return vNum(1.0 - (double)comp / (double)orig);
    });
    registerBuiltin("内存字典", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        for (const auto& s : memoryDict) l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("内存压缩阈值", 0, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        if (a.size() == 1) {
            double x = needNum(a[0], "内存压缩阈值", line);
            if (x < 0) x = 0;
            if (x > 1) x = 1;
            memoryMinSavings = x;
        }
        return vNum(memoryMinSavings);
    });
    registerBuiltin("内存对半切", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        size_t total = memoryStore.size() + memoryTemp.size();
        size_t target = total / 2;
        size_t removed = 0;
        for (auto it = memoryStore.begin(); it != memoryStore.end() && removed < target;) {
            it = memoryStore.erase(it);
            ++removed;
        }
        for (auto it = memoryTemp.begin(); it != memoryTemp.end() && removed < target;) {
            it = memoryTemp.erase(it);
            ++removed;
        }
        return vNum((double)removed);
    });
    registerBuiltin("内存清场", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        size_t n = memoryStore.size() + memoryTemp.size();
        memoryStore.clear();
        memoryTemp.clear();
        return vNum((double)n);
    });
    registerBuiltin("压缩", 1, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string what = needStr(a[0], "压缩", line);
        auto callOld = [&](const string& name, vector<Value> args) -> Value {
            Value fn;
            if (!globals->lookup(name, fn))
                throw WenError("运行时错误", line, 0, "内部缺少「" + name + "」");
            return callValue(fn, args, line);
        };
        if (what == "全部") return callOld("压缩全部内存", {});
        if (what == "牵引") return callOld("压缩牵引", {});
        if (what == "键") {
            if (a.size() < 2) throw WenError("运行时错误", line, 0, "压缩(\"键\", 键) 缺键");
            return callOld("压缩内存", {a[1]});
        }
        return callOld("压缩内存", {a[0]});
    });
    registerBuiltin("压缩率", 0, 0, [this](Interpreter&, vector<Value>&, int line) -> Value {
        Value fn;
        if (!globals->lookup("内存压缩率", fn))
            throw WenError("运行时错误", line, 0, "内存压缩率不存在");
        vector<Value> no;
        return callValue(fn, no, line);
    });
    registerBuiltin("清空", 1, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string what = needStr(a[0], "清空", line);
        auto callOld = [&](const string& name, vector<Value> args) -> Value {
            Value fn;
            if (!globals->lookup(name, fn))
                throw WenError("运行时错误", line, 0, "内部缺少「" + name + "」");
            return callValue(fn, args, line);
        };
        if (what == "全部") return callOld("内存清空", {});
        if (what == "常驻") return callOld("内存清场", {});
        if (what == "临时") return callOld("内存清临时", {});
        if (what == "压缩") {
            size_t n = memoryCompressed.size();
            memoryCompressed.clear();
            memoryOrigSize.clear();
            memoryDict.clear();
            memoryDictIndex.clear();
            return vNum((double)n);
        }
        if (what == "键") {
            if (a.size() < 2) throw WenError("运行时错误", line, 0, "清空(\"键\", 键) 缺键");
            return callOld("内存删", {a[1]});
        }
        return callOld("内存删", {a[0]});
    });
    registerBuiltin("内存缓存", 2, -1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = "cache|" + accelKey("", vector<Value>(a.begin() + 1, a.end()));
        auto it = memoryStore.find(key);
        if (it != memoryStore.end()) {
            ++memoryHits;
            return it->second;
        }
        auto cit = memoryCompressed.find(key);
        if (cit != memoryCompressed.end()) {
            string raw = decompressMemoryText(cit->second, line);
            JsonReader reader(raw, line);
            Value v = reader.parse();
            memoryStore[key] = v;
            memoryCompressed.erase(cit);
            memoryOrigSize.erase(key);
            ++memoryHits;
            return v;
        }
        vector<Value> args(a.begin() + 1, a.end());
        Value out = callValue(a[0], args, line);
        if (out.type == VT::Num || out.type == VT::Str || out.type == VT::Bool ||
            out.type == VT::None) {
            if (memoryCompress && isPulled(key)) {
                string raw = jsonDump(out, line);
                buildMemoryDict(raw);
                memoryOrigSize[key] = raw.size();
                memoryCompressed[key] = compressMemoryText(raw);
            } else {
                memoryStore[key] = out;
            }
        }
        return out;
    });

    // ---------- 跃迁库 ----------
    registerBuiltin("开启跃迁", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        leapAuto = true;
        return vBool(true);
    });
    registerBuiltin("关闭跃迁", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        leapAuto = false;
        return vBool(true);
    });
    registerBuiltin("跃迁查", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = leapKey(a[0], line);
        if (leapCache.count(key)) return vBool(true);
        string prefix = key + "|";
        for (const auto& kv : leapCache)
            if (kv.first.compare(0, prefix.size(), prefix) == 0) return vBool(true);
        return vBool(false);
    });
    registerBuiltin("跃迁记", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string key = leapKey(a[0], line);
        leapCache[key] = a[1];
        return a[1];
    });
    registerBuiltin("跃迁清空", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        leapCache.clear();
        leapHits = 0;
        leapMisses = 0;
        return vNone();
    });
    registerBuiltin("跃迁调用", 2, -1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string label = leapKey(a[0], line);
        vector<Value> args(a.begin() + 2, a.end());
        // 用户键只作标签；真正的缓存身份是“标签 + 参数”，不会再拿旧参数的结果。
        string key = label + accelKey("", args);
        auto it = leapCache.find(key);
        if (it != leapCache.end()) {
            ++leapHits;
            return it->second;
        }
        ++leapMisses;
        Value out = callValue(a[1], args, line);
        if (out.type == VT::Num || out.type == VT::Str || out.type == VT::Bool ||
            out.type == VT::None) {
            if (leapCache.size() >= leapLimit) leapCache.clear();
            leapCache[key] = out;
        }
        return out;
    });
    registerBuiltin("跃迁大小", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)leapCache.size());
    });
    registerBuiltin("跃迁命中", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)leapHits);
    });
    registerBuiltin("跃迁未命中", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)leapMisses);
    });
    registerBuiltin("跃迁复用率", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        long long total = leapHits + leapMisses;
        return vNum(total == 0 ? 0.0 : (double)leapHits / (double)total);
    });
    registerBuiltin("跃迁容量", 0, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        if (!a.empty()) {
            long long n = needIndex(a[0], "跃迁容量", line);
            if (n < 1) throw WenError("运行时错误", line, 0, "容量必须大于 0");
            leapLimit = (size_t)n;
            if (leapCache.size() > leapLimit) leapCache.clear();
        }
        return vNum((double)leapLimit);
    });
    registerBuiltin("跃迁快照", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        auto d = std::make_shared<Dict>();
        for (const auto& kv : leapCache) {
            if (kv.second.type != VT::Func) d->items[kv.first] = kv.second;
        }
        return vDict(d);
    });
    registerBuiltin("跃迁恢复", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        auto d = needDict(a[0], "跃迁恢复", line);
        leapCache.clear();
        for (const auto& kv : d->items) {
            if (kv.second.type != VT::Func) leapCache[kv.first] = kv.second;
        }
        return vNum((double)leapCache.size());
    });
    registerBuiltin("跃迁导出", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "跃迁导出", line);
        auto d = std::make_shared<Dict>();
        for (const auto& kv : leapCache) {
            if (kv.second.type != VT::Func) d->items[kv.first] = kv.second;
        }
        string text = jsonDump(vDict(d), line);
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, text, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vNum((double)d->items.size());
    });
    registerBuiltin("跃迁导入", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "跃迁导入", line);
        string text;
        try {
            text = readWholeFile(path);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        JsonReader reader(text, line);
        auto d = needDict(reader.parse(), "跃迁导入", line);
        leapCache.clear();
        for (const auto& kv : d->items) {
            if (kv.second.type != VT::Func) leapCache[kv.first] = kv.second;
        }
        return vNum((double)leapCache.size());
    });
    // ---------- 加速库 ----------
    registerBuiltin("开启加速", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        accelEnabled = true;
        return vBool(true);
    });
    registerBuiltin("关闭加速", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        accelEnabled = false;
        return vBool(true);
    });
    registerBuiltin("清空缓存", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        accelCache.clear();
        accelHits = 0;
        return vBool(true);
    });
    registerBuiltin("缓存大小", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)accelCache.size());
    });
    registerBuiltin("加速命中", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)accelHits);
    });

    registerBuiltin("指挥", 1, -1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "指挥", line);
        Value fn;
        if (!vm.globals->lookup(name, fn))
            throw WenError("运行时错误", line, 0, "指挥层不认识命令「" + name + "」");
        vector<Value> args(a.begin() + 1, a.end());
        return vm.callValue(fn, args, line);
    });
    registerBuiltin("指挥指令", 1, -1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "指挥指令", line);
        string s = name;
        if (a.size() > 1) {
            s += "(";
            for (size_t i = 1; i < a.size(); ++i) {
                if (i > 1) s += ", ";
                s += toText(a[i], true);
            }
            s += ")";
        }
        return vStr(s);
    });

    registerBuiltin("开启调用统计", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        callStats = true;
        return vBool(true);
    });
    registerBuiltin("关闭调用统计", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        callStats = false;
        return vBool(false);
    });
    registerBuiltin("调用次数", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "调用次数", line);
        auto it = callCounts.find(name);
        return vNum(it == callCounts.end() ? 0 : (double)it->second);
    });
    registerBuiltin("调用统计", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        auto d = std::make_shared<Dict>();
        for (const auto& kv : callCounts) d->items[kv.first] = vNum((double)kv.second);
        return vDict(d);
    });
    registerBuiltin("清空调用统计", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        callCounts.clear();
        return vNone();
    });
    registerBuiltin("别名", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string oldName = needStr(a[0], "别名", line);
        string newName = needStr(a[1], "别名", line);
        Value fn;
        if (!globals->lookup(oldName, fn))
            throw WenError("运行时错误", line, 0, "没有名字「" + oldName + "」");
        auto owner = symbolOwner.find(newName);
        if (owner != symbolOwner.end())
            throw WenError("库冲突", line, 0,
                           "别名「" + newName + "」已被「" + owner->second + "」占用");
        globals->vars[newName] = fn;
        auto oldOwner = symbolOwner.find(oldName);
        symbolOwner[newName] = oldOwner == symbolOwner.end() ? "别名" : oldOwner->second;
        return vBool(true);
    });
    registerBuiltin("取消别名", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "取消别名", line);
        auto owner = symbolOwner.find(name);
        if (owner == symbolOwner.end() || owner->second == "用户")
            throw WenError("运行时错误", line, 0, "「" + name + "」不是别名");
        globals->vars.erase(name);
        symbolOwner.erase(owner);
        return vBool(true);
    });
    registerBuiltin("文件后缀", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string use = needStr(a[0], "文件后缀", line);
        for (const auto& s : WEN_SUFFIXES)
            if (use == s.use) return vStr(s.ext);
        throw WenError("运行时错误", line, 0, "没有用途「" + use + "」的后缀");
    });
    registerBuiltin("源文件后缀", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        for (const char* suf : WEN_SOURCE_SUFFIXES) l->items.push_back(vStr(suf));
        return vList(l);
    });
    registerBuiltin("申请管理员", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        const char* env = std::getenv("WEN_ADMIN_TOKEN");
        string token = env ? env : "";
        if (token.empty())
            throw WenError("权限错误", line, 0,
                           "未配置管理员口令：先设置环境变量 WEN_ADMIN_TOKEN");
        if (needStr(a[0], "申请管理员", line) != token)
            throw WenError("权限错误", line, 0, "管理员口令不正确");
        adminGranted = true;
        return vBool(true);
    });
    registerBuiltin("是否管理员", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        return vBool(adminGranted);
    });
    registerBuiltin("退出管理员", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        adminGranted = false;
        dangerousMode = false;
        return vBool(true);
    });
    registerBuiltin("危险模式", 0, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        if (!a.empty()) {
            if (!adminGranted)
                throw WenError("权限错误", line, 0, "危险模式需要先申请管理员");
            dangerousMode = truthy(a[0]);
        }
        return vBool(dangerousMode);
    });
    registerBuiltin("并行", 2, -1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        if (a[0].type != VT::Func || !a[0].callable)
            throw WenError("运行时错误", line, 0, "并行需要函数");
        auto b = std::dynamic_pointer_cast<Builtin>(a[0].callable);
        if (!b || !b->cacheable())
            throw WenError("运行时错误", line, 0,
                           "并行只支持同一库的纯函数（数学/物理/概率等）");
        vector<vector<Value>> calls;
        for (size_t i = 1; i < a.size(); ++i) {
            if (b->maxArgs != 1 && a[i].type == VT::List && a[i].list) {
                calls.push_back(a[i].list->items);
            } else {
                calls.push_back({a[i]});
            }
        }
        if (calls.size() > 64)
            throw WenError("运行时错误", line, 0, "并行最多 64 组");
        vector<Value> results(calls.size());
        vector<string> errors(calls.size());
        unsigned hw = std::thread::hardware_concurrency();
        size_t threads = std::max(1u, std::min<unsigned>(hw ? hw : 1, (unsigned)calls.size()));
        threads = std::min<size_t>(threads, 8);
        vector<std::thread> pool;
        for (size_t t = 0; t < threads; ++t) {
            pool.emplace_back([&, t]() {
                for (size_t i = t; i < calls.size(); i += threads) {
                    try {
                        results[i] = b->fn(vm, calls[i], line);
                    } catch (WenError& e) {
                        errors[i] = e.what();
                    } catch (std::exception& e) {
                        errors[i] = e.what();
                    }
                }
            });
        }
        for (auto& th : pool) th.join();
        for (const auto& e : errors)
            if (!e.empty()) throw WenError("运行时错误", line, 0, "并行调用失败：" + e);
        auto out = std::make_shared<List>();
        out->items = std::move(results);
        return vList(out);
    });
    registerBuiltin("签名", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        return vStr(WEN_SIGNATURE);
    });
    registerBuiltin("库指引", 0, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        static const std::map<string, string> purpose = {
            {"数学库", "三角、对数、统计、组合、方程、随机"},
            {"概率库", "概率、分布、期望、权重分配"},
            {"物理库", "力学、热学、电学、光学、天体"},
            {"图形库", "面积、体积、表面积、距离"},
            {"渲染库", "面、涂鸦、渐变、体积光、导出后端"},
            {"跃迁库", "自动/纯函数缓存、统计、快照"},
            {"内存库", "常驻/临时内存、函数缓存、压缩、牵引"},
            {"兼容库", "翻译到 Python/JS/JSON/Java/字节码/C++/汇编"},
            {"管理员库", "管理员权限、危险模式"},
            {"函数库", "映射、过滤、折叠、排序、积分"},
            {"文件库", "读写、复制、重命名、目录"},
            {"输入库", "读整数/小数/按键、清屏"},
            {"集合库", "并/交/差/子集/对称差"},
            {"点阵库", "点集增删、平移、缩放"},
            {"频道库", "先进先出消息队列"},
            {"注入库", "调试注入变量/输入/代码"},
            {"可视化库", "折线/柱状/散点/饼图 SVG"},
            {"网络库", "抓取、下载、抓取 JSON"},
            {"网页库", "去标签、取标签/属性/链接/标题"},
            {"数据格式库", "JSON、CSV"}};
        auto count = [&](const string& lib) {
            auto it = libraryFunctions.find(lib);
            return it == libraryFunctions.end() ? 0 : (int)it->second.size();
        };
        if (a.empty()) {
            auto out = std::make_shared<List>();
            for (const auto& kv : purpose)
                out->items.push_back(vStr(kv.first + "（" + std::to_string(count(kv.first)) +
                                          "）：" + kv.second));
            return vList(out);
        }
        string lib = needStr(a[0], "库指引", line);
        auto pit = purpose.find(lib);
        if (pit == purpose.end())
            throw WenError("运行时错误", line, 0, "没有库「" + lib + "」");
        string s = lib + "（" + std::to_string(count(lib)) + "）：" + pit->second + "\n函数：";
        bool first = true;
        for (const auto& kv : functionModules()) {
            if (kv.second != lib) continue;
            if (!first) s += "、";
            first = false;
            s += kv.first;
        }
        return vStr(s);
    });
    registerBuiltin("找函数", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string q = needStr(a[0], "找函数", line);
        auto out = std::make_shared<List>();
        for (const auto& kv : functionModules()) {
            if (kv.first.find(q) != string::npos)
                out->items.push_back(vStr(kv.second + " / " + kv.first));
        }
        return vList(out);
    });
    registerBuiltin("新建缓冲", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "新建缓冲", line);
        if (n < 0 || n > 100000000LL)
            throw WenError("运行时错误", line, 0, "缓冲长度必须在 0 到 1 亿之间");
        int init = a.size() == 2 ? (int)needIndex(a[1], "新建缓冲", line) : 0;
        if (init < 0 || init > 255)
            throw WenError("运行时错误", line, 0, "缓冲初值必须在 0..255");
        return vBytes(std::make_shared<vector<unsigned char>>((size_t)n, (unsigned char)init));
    });
    registerBuiltin("缓冲长度", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum((double)needBytes(a[0], "缓冲长度", line)->size());
    });
    registerBuiltin("缓冲读", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto b = needBytes(a[0], "缓冲读", line);
        long long i = needIndex(a[1], "缓冲读", line);
        if (i < 0) i += (long long)b->size();
        if (i < 0 || (size_t)i >= b->size())
            throw WenError("运行时错误", line, 0, "缓冲下标越界");
        return vNum((double)(*b)[(size_t)i]);
    });
    registerBuiltin("缓冲写", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto b = needBytes(a[0], "缓冲写", line);
        long long i = needIndex(a[1], "缓冲写", line);
        long long v = needIndex(a[2], "缓冲写", line);
        if (i < 0) i += (long long)b->size();
        if (i < 0 || (size_t)i >= b->size())
            throw WenError("运行时错误", line, 0, "缓冲下标越界");
        if (v < 0 || v > 255)
            throw WenError("运行时错误", line, 0, "缓冲字节必须在 0..255");
        (*b)[(size_t)i] = (unsigned char)v;
        return vBool(true);
    });
    registerBuiltin("缓冲转文本", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto b = needBytes(a[0], "缓冲转文本", line);
        return vStr(string(reinterpret_cast<const char*>(b->data()), b->size()));
    });
    registerBuiltin("文本转缓冲", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "文本转缓冲", line);
        auto b = std::make_shared<vector<unsigned char>>(s.begin(), s.end());
        return vBytes(b);
    });
    registerBuiltin("缓冲切片", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto b = needBytes(a[0], "缓冲切片", line);
        long long start = needIndex(a[1], "缓冲切片", line);
        long long stop = needIndex(a[2], "缓冲切片", line);
        long long n = (long long)b->size();
        if (start < 0) start += n;
        if (stop < 0) stop += n;
        start = std::max(0LL, std::min(n, start));
        stop = std::max(0LL, std::min(n, stop));
        if (stop < start) stop = start;
        auto out = std::make_shared<vector<unsigned char>>(b->begin() + start, b->begin() + stop);
        return vBytes(out);
    });
    registerBuiltin("缓冲十六进制", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto b = needBytes(a[0], "缓冲十六进制", line);
        static const char* hx = "0123456789ABCDEF";
        string s;
        s.reserve(b->size() * 2);
        for (unsigned char c : *b) {
            s += hx[c >> 4];
            s += hx[c & 15];
        }
        return vStr(s);
    });
    registerBuiltin("十六进制转缓冲", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "十六进制转缓冲", line);
        if (s.size() % 2 != 0)
            throw WenError("运行时错误", line, 0, "十六进制长度必须是偶数");
        auto b = std::make_shared<vector<unsigned char>>();
        auto val = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw WenError("运行时错误", line, 0, "非法十六进制字符");
        };
        for (size_t i = 0; i < s.size(); i += 2)
            b->push_back((unsigned char)((val(s[i]) << 4) | val(s[i + 1])));
        return vBytes(b);
    });
    registerBuiltin("读二进制", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "读二进制", line);
        string raw;
        try {
            raw = readWholeFile(path);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        auto b = std::make_shared<vector<unsigned char>>(raw.begin(), raw.end());
        return vBytes(b);
    });
    registerBuiltin("写二进制", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "写二进制", line);
        auto b = needBytes(a[1], "写二进制", line);
        string raw(reinterpret_cast<const char*>(b->data()), b->size());
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, raw, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vBool(true);
    });
    // ---------- CPU / GPU ----------
    registerBuiltin("CPU核心数", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        unsigned n = std::thread::hardware_concurrency();
        return vNum(n ? (double)n : 1);
    });
    auto rangeLen = [](const Value& v) -> long long {
        double span = (v.rstop - v.rstart) / v.rstep;
        return span <= 0 ? 0 : (long long)std::ceil(span - 1e-12);
    };
    registerBuiltin("CPU并行求和", 1, 1, [rangeLen](Interpreter&, vector<Value>& a, int line) -> Value {
        if (a[0].type == VT::Range) {
            long long n = rangeLen(a[0]);
            if (n <= 0) return vNum(0);
            return vNum((double)n *
                        (2.0 * a[0].rstart + (double)(n - 1) * a[0].rstep) / 2.0);
        }
        auto l = needList(a[0], "CPU并行求和", line);
        vector<double> xs;
        for (const auto& v : l->items) xs.push_back(needNum(v, "CPU并行求和", line));
        return vNum(parallelReduceNums(xs, 0));
    });
    registerBuiltin("CPU并行最大", 1, 1, [rangeLen](Interpreter&, vector<Value>& a, int line) -> Value {
        if (a[0].type == VT::Range) {
            long long n = rangeLen(a[0]);
            if (n <= 0) throw WenError("运行时错误", line, 0, "范围不能为空");
            double last = a[0].rstart + (double)(n - 1) * a[0].rstep;
            return vNum(a[0].rstep > 0 ? last : a[0].rstart);
        }
        auto l = needList(a[0], "CPU并行最大", line);
        vector<double> xs;
        for (const auto& v : l->items) xs.push_back(needNum(v, "CPU并行最大", line));
        if (xs.empty()) throw WenError("运行时错误", line, 0, "列表不能为空");
        return vNum(parallelReduceNums(xs, 1));
    });
    registerBuiltin("CPU并行最小", 1, 1, [rangeLen](Interpreter&, vector<Value>& a, int line) -> Value {
        if (a[0].type == VT::Range) {
            long long n = rangeLen(a[0]);
            if (n <= 0) throw WenError("运行时错误", line, 0, "范围不能为空");
            double last = a[0].rstart + (double)(n - 1) * a[0].rstep;
            return vNum(a[0].rstep > 0 ? a[0].rstart : last);
        }
        auto l = needList(a[0], "CPU并行最小", line);
        vector<double> xs;
        for (const auto& v : l->items) xs.push_back(needNum(v, "CPU并行最小", line));
        if (xs.empty()) throw WenError("运行时错误", line, 0, "列表不能为空");
        return vNum(parallelReduceNums(xs, 2));
    });    // ---------- 时间 / 编码 / 正则 ----------
    registerBuiltin("现在", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        std::time_t t = std::time(nullptr);
        std::tm lt = *std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        return vStr(buf);
    });
    registerBuiltin("时间戳", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        return vNum((double)std::time(nullptr));
    });
    registerBuiltin("毫秒时间", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        using namespace std::chrono;
        return vNum((double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    });
    registerBuiltin("格式化时间", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        std::time_t t = (std::time_t)needNum(a[0], "格式化时间", line);
        std::tm lt = *std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        return vStr(buf);
    });
    registerBuiltin("解析时间", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "解析时间", line);
        std::tm tm{};
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ss.fail()) throw WenError("运行时错误", line, 0, "时间格式应为 YYYY-MM-DD HH:MM:SS");
        return vNum((double)std::mktime(&tm));
    });
    registerBuiltin("休眠", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long ms = needIndex(a[0], "休眠", line);
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return vNone();
    });
    registerBuiltin("Base64编码", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        string s = needStr(a[0], "Base64编码", line), out;
        int val = 0, bits = -6;
        for (unsigned char c : s) {
            val = (val << 8) + c;
            bits += 8;
            while (bits >= 0) {
                out += T[(val >> bits) & 63];
                bits -= 6;
            }
        }
        if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 63];
        while (out.size() % 4) out += '=';
        return vStr(out);
    });
    registerBuiltin("Base64解码", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        static const string T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        string s = needStr(a[0], "Base64解码", line), out;
        int val = 0, bits = -8;
        for (unsigned char c : s) {
            if (c == '=') break;
            auto p = T.find((char)c);
            if (p == string::npos) throw WenError("运行时错误", line, 0, "Base64 含非法字符");
            val = (val << 6) + (int)p;
            bits += 6;
            if (bits >= 0) {
                out += (char)((val >> bits) & 255);
                bits -= 8;
            }
        }
        return vStr(out);
    });
    registerBuiltin("URL编码", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        static const char* H = "0123456789ABCDEF";
        string s = needStr(a[0], "URL编码", line), out;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
            else {
                out += '%';
                out += H[c >> 4];
                out += H[c & 15];
            }
        }
        return vStr(out);
    });
    registerBuiltin("URL解码", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "URL解码", line), out;
        auto hex = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw WenError("运行时错误", line, 0, "URL 编码含非法字符");
        };
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%') {
                if (i + 2 >= s.size()) throw WenError("运行时错误", line, 0, "URL 编码不完整");
                out += (char)((hex(s[i + 1]) << 4) | hex(s[i + 2]));
                i += 2;
            } else if (s[i] == '+') out += ' ';
            else out += s[i];
        }
        return vStr(out);
    });
    registerBuiltin("正则匹配", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        try {
            std::regex re(needStr(a[1], "正则匹配", line));
            return vBool(std::regex_search(needStr(a[0], "正则匹配", line), re));
        } catch (std::regex_error& e) {
            throw WenError("运行时错误", line, 0, string("正则表达式错误：") + e.what());
        }
    });
    registerBuiltin("正则查找", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        try {
            std::regex re(needStr(a[1], "正则查找", line));
            string s = needStr(a[0], "正则查找", line);
            auto out = std::make_shared<List>();
            for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
                out->items.push_back(vStr(it->str()));
            return vList(out);
        } catch (std::regex_error& e) {
            throw WenError("运行时错误", line, 0, string("正则表达式错误：") + e.what());
        }
    });
    registerBuiltin("正则替换", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        try {
            std::regex re(needStr(a[1], "正则替换", line));
            return vStr(std::regex_replace(needStr(a[0], "正则替换", line), re,
                                           needStr(a[2], "正则替换", line)));
        } catch (std::regex_error& e) {
            throw WenError("运行时错误", line, 0, string("正则表达式错误：") + e.what());
        }
    });
    registerBuiltin("正则分割", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        try {
            std::regex re(needStr(a[1], "正则分割", line));
            string s = needStr(a[0], "正则分割", line);
            auto out = std::make_shared<List>();
            std::sregex_token_iterator it(s.begin(), s.end(), re, -1), end;
            for (; it != end; ++it) out->items.push_back(vStr(*it));
            return vList(out);
        } catch (std::regex_error& e) {
            throw WenError("运行时错误", line, 0, string("正则表达式错误：") + e.what());
        }
    });
    // ---------- 路径 / 日志 / 哈希 ----------
    registerBuiltin("路径拼接", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr((fs::u8path(needStr(a[0], "路径拼接", line)) /
                     fs::u8path(needStr(a[1], "路径拼接", line))).string());
    });
    registerBuiltin("路径目录", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(fs::u8path(needStr(a[0], "路径目录", line)).parent_path().string());
    });
    registerBuiltin("路径文件名", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(fs::u8path(needStr(a[0], "路径文件名", line)).filename().string());
    });
    registerBuiltin("路径扩展名", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(fs::u8path(needStr(a[0], "路径扩展名", line)).extension().string());
    });
    registerBuiltin("路径绝对", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        std::error_code ec;
        auto p = fs::absolute(fs::u8path(needStr(a[0], "路径绝对", line)), ec);
        if (ec) throw WenError("运行时错误", line, 0, "路径绝对失败：" + ec.message());
        return vStr(p.string());
    });
    auto logStamp = []() {
        std::time_t t = std::time(nullptr);
        std::tm lt = *std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        return string(buf);
    };
    registerBuiltin("日志时间", 0, 0, [logStamp](Interpreter&, vector<Value>&, int) -> Value {
        return vStr(logStamp());
    });
    registerBuiltin("日志", 1, 1, [logStamp](Interpreter&, vector<Value>& a, int line) -> Value {
        std::cout << "[" << logStamp() << "] " << needStr(a[0], "日志", line) << "\n";
        return vNone();
    });
    registerBuiltin("写日志", 2, 2, [logStamp](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "写日志", line);
        string text = "[" + logStamp() + "] " + needStr(a[1], "写日志", line) + "\n";
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, text, true);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vBool(true);
    });
    registerBuiltin("清空日志", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "清空日志", line);
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, "", false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vBool(true);
    });
    auto fnv1a = [](const string& s) {
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    };
    registerBuiltin("哈希", 1, 1, [fnv1a](Interpreter&, vector<Value>& a, int line) -> Value {
        unsigned long long h = fnv1a(needStr(a[0], "哈希", line));
        std::ostringstream s;
        s << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << h;
        return vStr(s.str());
    });
    registerBuiltin("文件哈希", 1, 1, [fnv1a](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "文件哈希", line);
        string raw;
        try {
            raw = readWholeFile(path);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        unsigned long long h = fnv1a(raw);
        std::ostringstream s;
        s << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << h;
        return vStr(s.str());
    });
    registerBuiltin("校验和", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "校验和", line);
        unsigned int crc = 0xFFFFFFFFu;
        for (unsigned char c : s) {
            crc ^= c;
            for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
        return vNum((double)(crc ^ 0xFFFFFFFFu));
    });
    // ---------- 数学 ----------
    auto m1 = [this](const string& name, double (*f)(double)) {
        registerBuiltin(name, 1, 1,
                        [name, f](Interpreter&, vector<Value>& a, int line) -> Value {
                            return vNum(f(needNum(a[0], name, line)));
                        });
    };
    m1("绝对值", [](double x) { return std::fabs(x); });
    m1("取整", [](double x) { return std::round(x); });
    m1("向下取整", [](double x) { return std::floor(x); });
    m1("向上取整", [](double x) { return std::ceil(x); });
    m1("指数", [](double x) { return std::exp(x); });
    m1("正弦", [](double x) { return std::sin(x); });
    m1("余弦", [](double x) { return std::cos(x); });
    m1("正切", [](double x) { return std::tan(x); });
    m1("双曲正弦", [](double x) { return std::sinh(x); });
    m1("双曲余弦", [](double x) { return std::cosh(x); });
    m1("双曲正切", [](double x) { return std::tanh(x); });
    m1("角度转弧度", [](double x) { return x * std::acos(-1.0) / 180.0; });
    m1("弧度转角度", [](double x) { return x * 180.0 / std::acos(-1.0); });

    registerBuiltin("平方根", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "平方根", line);
        if (x < 0)
            throw WenError("运行时错误", line, 0,
                           "负数不能开平方根（结果是复数；请用解二次方程）");
        return vNum(std::sqrt(x));
    });
    registerBuiltin("幂", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "幂", line);
        double y = needNum(a[1], "幂", line);
        double r = std::pow(x, y);
        if (std::isnan(r))
            throw WenError("运行时错误", line, 0, "幂运算的结果不是实数");
        return vNum(r);
    });
    registerBuiltin("对数", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "对数", line);
        if (x <= 0) throw WenError("运行时错误", line, 0, "对数的真数必须大于 0");
        if (a.size() == 1) return vNum(std::log(x));
        double b = needNum(a[1], "对数", line);
        if (b <= 0 || b == 1)
            throw WenError("运行时错误", line, 0, "对数的底必须大于 0 且不等于 1");
        return vNum(std::log(x) / std::log(b));
    });
    registerBuiltin("对数10", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "对数10", line);
        if (x <= 0) throw WenError("运行时错误", line, 0, "对数的真数必须大于 0");
        return vNum(std::log10(x));
    });
    registerBuiltin("反正弦", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "反正弦", line);
        if (x < -1 || x > 1)
            throw WenError("运行时错误", line, 0, "反正弦的输入必须在 -1 到 1 之间");
        return vNum(std::asin(x));
    });
    registerBuiltin("反余弦", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "反余弦", line);
        if (x < -1 || x > 1)
            throw WenError("运行时错误", line, 0, "反余弦的输入必须在 -1 到 1 之间");
        return vNum(std::acos(x));
    });
    registerBuiltin("反正切", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(std::atan(needNum(a[0], "反正切", line)));
    });
    registerBuiltin("反正切2", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double y = needNum(a[0], "反正切2", line);
        double x = needNum(a[1], "反正切2", line);
        return vNum(std::atan2(y, x));
    });
    registerBuiltin("符号", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "符号", line);
        return vNum((x > 0) - (x < 0));
    });

    registerBuiltin("随机", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return vNum(dist(rng));
    });
    registerBuiltin("随机整数", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        long long lo = needIndex(a[0], "随机整数", line);
        long long hi = needIndex(a[1], "随机整数", line);
        if (lo > hi) std::swap(lo, hi);
        std::uniform_int_distribution<long long> dist(lo, hi);
        return vNum((double)dist(rng));
    });

    auto gcdOf = [](long long a, long long b) {
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        while (b) {
            long long t = a % b;
            a = b;
            b = t;
        }
        return a;
    };
    registerBuiltin("阶乘", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "阶乘", line);
        if (n < 0)
            throw WenError("运行时错误", line, 0, "「阶乘」只接受非负整数");
        if (n > 100000)
            throw WenError("运行时错误", line, 0, "「阶乘」的参数太大（上限 100000）");
        double r = 1;
        for (long long i = 2; i <= n && !std::isinf(r); ++i) r *= (double)i;
        return vNum(r);
    });
    registerBuiltin("最大公约数", 2, 2, [gcdOf](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum((double)gcdOf(needIndex(a[0], "最大公约数", line),
                                  needIndex(a[1], "最大公约数", line)));
    });
    registerBuiltin("最小公倍数", 2, 2, [gcdOf](Interpreter&, vector<Value>& a, int line) -> Value {
        long long x = needIndex(a[0], "最小公倍数", line);
        long long y = needIndex(a[1], "最小公倍数", line);
        if (x == 0 || y == 0) return vNum(0);
        long long g = gcdOf(x, y);
        return vNum((double)(x / g * y));
    });
    registerBuiltin("是否素数", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "是否素数", line);
        if (n < 2) return vBool(false);
        if (n % 2 == 0) return vBool(n == 2);
        for (long long i = 3; i * i <= n; i += 2)
            if (n % i == 0) return vBool(false);
        return vBool(true);
    });
    registerBuiltin("质因数分解", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "质因数分解", line);
        if (n < 2)
            throw WenError("运行时错误", line, 0, "「质因数分解」只接受大于 1 的整数");
        auto l = std::make_shared<List>();
        for (long long i = 2; i * i <= n; ++i) {
            while (n % i == 0) {
                l->items.push_back(vNum((double)i));
                n /= i;
            }
        }
        if (n > 1) l->items.push_back(vNum((double)n));
        return vList(l);
    });
    registerBuiltin("平均数", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "平均数", line);
        if (l->items.empty())
            throw WenError("运行时错误", line, 0, "「平均数」不能对空列表求值");
        double sum = 0;
        for (const auto& v : l->items) sum += needNum(v, "平均数", line);
        return vNum(sum / (double)l->items.size());
    });
    registerBuiltin("中位数", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "中位数", line);
        if (l->items.empty())
            throw WenError("运行时错误", line, 0, "「中位数」不能对空列表求值");
        vector<double> xs;
        for (const auto& v : l->items) xs.push_back(needNum(v, "中位数", line));
        std::sort(xs.begin(), xs.end());
        size_t n = xs.size();
        if (n % 2) return vNum(xs[n / 2]);
        return vNum((xs[n / 2 - 1] + xs[n / 2]) / 2.0);
    });
    registerBuiltin("方差", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "方差", line);
        if (l->items.empty())
            throw WenError("运行时错误", line, 0, "「方差」不能对空列表求值");
        double sum = 0;
        for (const auto& v : l->items) sum += needNum(v, "方差", line);
        double mean = sum / (double)l->items.size();
        double acc = 0;
        for (const auto& v : l->items) {
            double d = needNum(v, "方差", line) - mean;
            acc += d * d;
        }
        bool sample = a.size() == 2 && truthy(a[1]);
        long long n = (long long)l->items.size();
        if (sample && n < 2)
            throw WenError("运行时错误", line, 0, "样本方差至少需要 2 个数");
        return vNum(acc / (double)(sample ? n - 1 : n));
    });
    registerBuiltin("样本方差", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "样本方差", line);
        long long n = (long long)l->items.size();
        if (n < 2)
            throw WenError("运行时错误", line, 0, "样本方差至少需要 2 个数");
        double sum = 0;
        for (const auto& v : l->items) sum += needNum(v, "样本方差", line);
        double mean = sum / (double)n;
        double acc = 0;
        for (const auto& v : l->items) {
            double d = needNum(v, "样本方差", line) - mean;
            acc += d * d;
        }
        return vNum(acc / (double)(n - 1));
    });
    registerBuiltin("标准差", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "标准差", line);
        if (l->items.empty())
            throw WenError("运行时错误", line, 0, "「标准差」不能对空列表求值");
        double sum = 0;
        for (const auto& v : l->items) sum += needNum(v, "标准差", line);
        double mean = sum / (double)l->items.size();
        double acc = 0;
        for (const auto& v : l->items) {
            double d = needNum(v, "标准差", line) - mean;
            acc += d * d;
        }
        return vNum(std::sqrt(acc / (double)l->items.size()));
    });
    registerBuiltin("样本标准差", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "样本标准差", line);
        long long n = (long long)l->items.size();
        if (n < 2)
            throw WenError("运行时错误", line, 0, "样本标准差至少需要 2 个数");
        double sum = 0;
        for (const auto& v : l->items) sum += needNum(v, "样本标准差", line);
        double mean = sum / (double)n;
        double acc = 0;
        for (const auto& v : l->items) {
            double d = needNum(v, "样本标准差", line) - mean;
            acc += d * d;
        }
        return vNum(std::sqrt(acc / (double)(n - 1)));
    });
    registerBuiltin("矩阵乘", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto readMat = [&](const Value& v, const string& name) {
            auto rows = needList(v, name, line);
            vector<vector<double>> m;
            size_t cols = 0;
            for (size_t i = 0; i < rows->items.size(); ++i) {
                auto row = needList(rows->items[i], name, line);
                if (i == 0) cols = row->items.size();
                if (row->items.size() != cols)
                    throw WenError("运行时错误", line, 0, name + " 第 " + std::to_string(i + 1) +
                                                           " 行列数不一致");
                vector<double> r;
                for (const auto& x : row->items) r.push_back(needNum(x, name, line));
                m.push_back(r);
            }
            return m;
        };
        auto A = readMat(a[0], "矩阵甲");
        auto B = readMat(a[1], "矩阵乙");
        if (A.empty() || B.empty() || A[0].size() != B.size())
            throw WenError("运行时错误", line, 0, "矩阵乘维度不匹配");
        size_t n = A.size(), m = B.size(), p = B[0].size();
        auto out = std::make_shared<List>();
        for (size_t i = 0; i < n; ++i) {
            auto row = std::make_shared<List>();
            for (size_t j = 0; j < p; ++j) {
                double s = 0;
                for (size_t k = 0; k < m; ++k) s += A[i][k] * B[k][j];
                row->items.push_back(vNum(s));
            }
            out->items.push_back(vList(row));
        }
        return vList(out);
    });
    registerBuiltin("组合数", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "组合数", line);
        long long k = needIndex(a[1], "组合数", line);
        if (n < 0 || k < 0 || k > n)
            throw WenError("运行时错误", line, 0, "组合数要求 0 ≤ k ≤ n");
        return vNum(combCount(n, k));
    });
    registerBuiltin("排列数", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "排列数", line);
        long long k = needIndex(a[1], "排列数", line);
        if (n < 0 || k < 0 || k > n)
            throw WenError("运行时错误", line, 0, "排列数要求 0 ≤ k ≤ n");
        double r = 1;
        for (long long i = 0; i < k; ++i) r *= (double)(n - i);
        return vNum(r);
    });
    registerBuiltin("格式化", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "格式化", line);
        long long digits = a.size() == 2 ? needIndex(a[1], "格式化", line) : 2;
        if (digits < 0) digits = 0;
        if (digits > 15) digits = 15;
        std::ostringstream os;
        os << std::fixed << std::setprecision((int)digits) << x;
        return vStr(os.str());
    });

    auto minmax = [this](const string& name, bool wantMax) {
        registerBuiltin(name, 1, -1,
                        [name, wantMax](Interpreter&, vector<Value>& a, int line) -> Value {
            vector<Value> items;
            if (a.size() == 1 && a[0].type == VT::List && a[0].list)
                items = a[0].list->items;
            else
                items = a;
            if (items.empty())
                throw WenError("运行时错误", line, 0, "「" + name + "」没有参数");
            bool allNum = true, allStr = true;
            for (const auto& v : items) {
                if (v.type != VT::Num) allNum = false;
                if (v.type != VT::Str) allStr = false;
            }
            if (!allNum && !allStr)
                throw WenError("运行时错误", line, 0,
                               "「" + name + "」只支持全是数字或全是文本");
            Value best = items[0];
            for (size_t i = 1; i < items.size(); ++i) {
                bool better = allNum ? (wantMax ? items[i].num > best.num : items[i].num < best.num)
                                     : (wantMax ? items[i].str > best.str : items[i].str < best.str);
                if (better) best = items[i];
            }
            return best;
        });
    };
    minmax("最大", true);
    minmax("最小", false);

    registerBuiltin("求和", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "求和", line);
        double sum = 0;
        for (const auto& v : l->items) sum += needNum(v, "求和", line);
        return vNum(sum);
    });

    // ---------- 二次 / 多元数学 ----------
    registerBuiltin("解二次方程", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double A = needNum(a[0], "解二次方程", line);
        double B = needNum(a[1], "解二次方程", line);
        double C = needNum(a[2], "解二次方程", line);
        if (std::fabs(A) < 1e-15)
            throw WenError("运行时错误", line, 0, "二次项系数 a 不能为 0");
        double D = B * B - 4 * A * C;
        auto root = [&](double re, double im) {
            auto r = std::make_shared<Dict>();
            r->items["实部"] = vNum(re);
            r->items["虚部"] = vNum(im);
            return vDict(r);
        };
        auto out = std::make_shared<Dict>();
        out->items["判别式"] = vNum(D);
        if (D >= 0) {
            double s = std::sqrt(D);
            out->items["根1"] = root((-B + s) / (2 * A), 0);
            out->items["根2"] = root((-B - s) / (2 * A), 0);
            out->items["实根数"] = vNum(D > 1e-12 ? 2 : 1);
        } else {
            double s = std::sqrt(-D);
            out->items["根1"] = root(-B / (2 * A), s / (2 * A));
            out->items["根2"] = root(-B / (2 * A), -s / (2 * A));
            out->items["实根数"] = vNum(0);
        }
        return vDict(out);
    });
    registerBuiltin("二次函数", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double A = needNum(a[0], "二次函数", line);
        double B = needNum(a[1], "二次函数", line);
        double C = needNum(a[2], "二次函数", line);
        double x = needNum(a[3], "二次函数", line);
        return vNum(A * x * x + B * x + C);
    });
    registerBuiltin("多项式求值", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "多项式求值", line);
        double x = needNum(a[1], "多项式求值", line);
        double result = 0;
        for (long long i = (long long)l->items.size() - 1; i >= 0; --i)
            result = result * x + needNum(l->items[(size_t)i], "多项式求值", line);
        return vNum(result);
    });
    registerBuiltin("二元二次求值", 8, 8, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double A = needNum(a[0], "二元二次求值", line);
        double B = needNum(a[1], "二元二次求值", line);
        double C = needNum(a[2], "二元二次求值", line);
        double D = needNum(a[3], "二元二次求值", line);
        double E = needNum(a[4], "二元二次求值", line);
        double F = needNum(a[5], "二元二次求值", line);
        double x = needNum(a[6], "二元二次求值", line);
        double y = needNum(a[7], "二元二次求值", line);
        return vNum(A * x * x + B * x * y + C * y * y + D * x + E * y + F);
    });
    registerBuiltin("解二元一次方程组", 6, 6,
                    [](Interpreter&, vector<Value>& a, int line) -> Value {
        double a1 = needNum(a[0], "解二元一次方程组", line);
        double b1 = needNum(a[1], "解二元一次方程组", line);
        double c1 = needNum(a[2], "解二元一次方程组", line);
        double a2 = needNum(a[3], "解二元一次方程组", line);
        double b2 = needNum(a[4], "解二元一次方程组", line);
        double c2 = needNum(a[5], "解二元一次方程组", line);
        double det = a1 * b2 - a2 * b1;
        if (std::fabs(det) < 1e-15)
            throw WenError("运行时错误", line, 0, "方程组没有唯一解（行列式为 0）");
        double x = (c1 * b2 - c2 * b1) / det;
        double y = (a1 * c2 - a2 * c1) / det;
        auto l = std::make_shared<List>();
        l->items.push_back(vNum(x));
        l->items.push_back(vNum(y));
        return vList(l);
    });
    registerBuiltin("解线性方程组", 2, 2,
                    [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto rows = needList(a[0], "解线性方程组", line);
        auto vec = needList(a[1], "解线性方程组", line);
        size_t n = rows->items.size();
        if (n == 0 || vec->items.size() != n)
            throw WenError("运行时错误", line, 0, "系数矩阵必须是 n×n，常数向量长度必须是 n");
        vector<vector<double>> m(n, vector<double>(n + 1, 0));
        for (size_t i = 0; i < n; ++i) {
            auto row = needList(rows->items[i], "解线性方程组", line);
            if (row->items.size() != n)
                throw WenError("运行时错误", line, 0, "系数矩阵必须是 n×n");
            for (size_t j = 0; j < n; ++j)
                m[i][j] = needNum(row->items[j], "解线性方程组", line);
            m[i][n] = needNum(vec->items[i], "解线性方程组", line);
        }
        for (size_t col = 0; col < n; ++col) {
            size_t pivot = col;
            for (size_t r = col + 1; r < n; ++r)
                if (std::fabs(m[r][col]) > std::fabs(m[pivot][col])) pivot = r;
            if (std::fabs(m[pivot][col]) < 1e-12)
                throw WenError("运行时错误", line, 0, "方程组没有唯一解");
            std::swap(m[col], m[pivot]);
            for (size_t r = col + 1; r < n; ++r) {
                double factor = m[r][col] / m[col][col];
                for (size_t c = col; c <= n; ++c) m[r][c] -= factor * m[col][c];
            }
        }
        vector<double> x(n, 0);
        for (long long i = (long long)n - 1; i >= 0; --i) {
            double s = m[(size_t)i][n];
            for (size_t j = (size_t)i + 1; j < n; ++j) s -= m[(size_t)i][j] * x[j];
            x[(size_t)i] = s / m[(size_t)i][(size_t)i];
        }
        auto out = std::make_shared<List>();
        for (double v : x) out->items.push_back(vNum(v));
        return vList(out);
    });

    // ---------- 文件 ----------
    registerBuiltin("读文件", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(readWholeFile(needStr(a[0], "读文件", line)));
    });
    registerBuiltin("写文件", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        writeWholeFile(needStr(a[0], "写文件", line),
                       needStr(a[1], "写文件", line), false);
        return vBool(true);
    });
    registerBuiltin("追加文件", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        writeWholeFile(needStr(a[0], "追加文件", line),
                       needStr(a[1], "追加文件", line), true);
        return vBool(true);
    });
    registerBuiltin("文件存在", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "文件存在", line);
        std::error_code ec;
        bool ok = fs::exists(fs::u8path(path), ec);
        return vBool(ok && !ec);
    });
    registerBuiltin("删除文件", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "删除文件", line);
        std::error_code ec;
        bool ok = fs::remove(fs::u8path(path), ec);
        return vBool(ok && !ec);
    });
    registerBuiltin("文件大小", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "文件大小", line);
        std::error_code ec;
        auto n = fs::file_size(fs::u8path(path), ec);
        if (ec) throw WenError("文件错误", line, 0, "读不到文件大小：" + path);
        return vNum((double)n);
    });
    registerBuiltin("列目录", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "列目录", line);
        std::error_code ec;
        auto out = std::make_shared<List>();
        vector<string> names;
        for (fs::directory_iterator it(fs::u8path(path), ec), end; !ec && it != end;
             it.increment(ec)) {
            names.push_back(it->path().filename().u8string());
        }
        if (ec) throw WenError("文件错误", line, 0, "读不到目录：" + path);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) out->items.push_back(vStr(n));
        return vList(out);
    });
    registerBuiltin("复制文件", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string from = needStr(a[0], "复制文件", line);
        string to = needStr(a[1], "复制文件", line);
        std::error_code ec;
        ensureParentDirectory(to);
        fs::copy_file(fs::u8path(from), fs::u8path(to),
                      fs::copy_options::overwrite_existing, ec);
        return vBool(!ec);
    });
    registerBuiltin("重命名", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string from = needStr(a[0], "重命名", line);
        string to = needStr(a[1], "重命名", line);
        std::error_code ec;
        fs::rename(fs::u8path(from), fs::u8path(to), ec);
        return vBool(!ec);
    });
    registerBuiltin("创建目录", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "创建目录", line);
        std::error_code ec;
        bool ok = fs::create_directories(fs::u8path(path), ec);
        return vBool(ok || fs::exists(fs::u8path(path)));
    });

    // ---------- 网络库 ----------
    registerBuiltin("安全抓取", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string url = needStr(a[0], "安全抓取", line);
        if (unsafeNetHost(url))
            throw WenError("运行时错误", line, 0, "安全抓取拒绝内网/本机地址：" + url);
        int timeout = a.size() == 2 ? (int)needIndex(a[1], "安全抓取", line) : 15000;
        return vStr(httpGetText(url, timeout, line));
    });
    registerBuiltin("限速抓取", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string url = needStr(a[0], "限速抓取", line);
        long long ms = needIndex(a[1], "限速抓取", line);
        if (ms < 0) ms = 0;
        if (ms > 60000) ms = 60000;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        int timeout = a.size() == 3 ? (int)needIndex(a[2], "限速抓取", line) : 15000;
        return vStr(httpGetText(url, timeout, line));
    });
    registerBuiltin("乱码解析", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string s = needStr(a[0], "乱码解析", line);
        if (s.empty()) return vStr(s);
        static const std::map<string, string> known = {
            {"涓枃", "中文"}, {"浣犲ソ", "你好"}, {"鏂囧瓧", "文字"},
            {"涓枃瀛", "中文学"}, {"涓枃瀛楃", "中文字"}};
        auto kit = known.find(s);
        if (kit != known.end()) return vStr(kit->second);
        auto validUtf8 = [](const string& x) {
            return x.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, x.data(),
                                                    (int)x.size(), nullptr, 0) > 0;
        };
        if (!validUtf8(s)) {
            string fixed = codePageToUtf8(s, 936);
            return vStr(fixed.empty() ? s : fixed);
        }
        // UTF-8 文本被当成 GBK：先编回 GBK 字节，再按 UTF-8 解。
        std::wstring w = utf8ToWide(s);
        int n = WideCharToMultiByte(936, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            string gbk(n, 0);
            WideCharToMultiByte(936, 0, w.data(), (int)w.size(), &gbk[0], n, nullptr, nullptr);
            string fixed = codePageToUtf8(gbk, 65001);
            if (!fixed.empty() && fixed != s && validUtf8(fixed)) return vStr(fixed);
        }
        return vStr(s);
    });
    registerBuiltin("二进制破坏", 1, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        if (!adminGranted || !dangerousMode)
            throw WenError("权限错误", line, 0, "二进制破坏需要管理员 + 危险模式");
        string s = needStr(a[0], "二进制破坏", line);
        string key = a.size() > 1 ? needStr(a[1], "二进制破坏", line) : "wen";
        if (key.empty()) key = "wen";
        static const char* H = "0123456789ABCDEF";
        string out = "WENB:";
        for (size_t i = 0; i < s.size(); ++i) {
            unsigned char c = (unsigned char)s[i] ^ (unsigned char)key[i % key.size()];
            out += H[c >> 4];
            out += H[c & 15];
        }
        return vStr(out);
    });
    registerBuiltin("二进制恢复", 1, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        if (!adminGranted || !dangerousMode)
            throw WenError("权限错误", line, 0, "二进制恢复需要管理员 + 危险模式");
        string s = needStr(a[0], "二进制恢复", line);
        if (s.rfind("WENB:", 0) != 0)
            throw WenError("运行时错误", line, 0, "不是二进制破坏数据");
        string key = a.size() > 1 ? needStr(a[1], "二进制恢复", line) : "wen";
        if (key.empty()) key = "wen";
        auto val = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw WenError("运行时错误", line, 0, "破坏数据含非法字符");
        };
        string hex = s.substr(5);
        if (hex.size() % 2) throw WenError("运行时错误", line, 0, "破坏数据长度错误");
        string out;
        for (size_t i = 0; i < hex.size(); i += 2) {
            unsigned char c = (unsigned char)((val(hex[i]) << 4) | val(hex[i + 1]));
            out += (char)(c ^ (unsigned char)key[(i / 2) % key.size()]);
        }
        return vStr(out);
    });
    registerBuiltin("抓取", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string url = needStr(a[0], "抓取", line);
        int timeout = a.size() == 2 ? (int)needIndex(a[1], "抓取", line) : 15000;
        return vStr(httpGetText(url, timeout, line));
    });
    registerBuiltin("下载", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string url = needStr(a[0], "下载", line);
        string path = needStr(a[1], "下载", line);
        int timeout = a.size() == 3 ? (int)needIndex(a[2], "下载", line) : 15000;
        string body = httpGetText(url, timeout, line);
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, body, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });
    registerBuiltin("抓取JSON", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string url = needStr(a[0], "抓取JSON", line);
        int timeout = a.size() == 2 ? (int)needIndex(a[1], "抓取JSON", line) : 15000;
        string body = httpGetText(url, timeout, line);
        JsonReader reader(body, line);
        return reader.parse();
    });

    // ---------- JSON 库 ----------
    registerBuiltin("解析JSON", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string text = needStr(a[0], "解析JSON", line);
        JsonReader reader(text, line);
        return reader.parse();
    });
    registerBuiltin("生成JSON", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(jsonDump(a[0], line));
    });
    registerBuiltin("解析CSV", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string text = needStr(a[0], "解析CSV", line);
        vector<vector<string>> rows;
        vector<string> row;
        string field;
        bool quoted = false;
        for (size_t i = 0; i <= text.size(); ++i) {
            char c = i < text.size() ? text[i] : '\n';
            if (quoted) {
                if (c == '"') {
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        field += '"';
                        ++i;
                    } else {
                        quoted = false;
                    }
                } else {
                    field += c;
                }
            } else if (c == '"') {
                quoted = true;
            } else if (c == ',') {
                row.push_back(field);
                field.clear();
            } else if (c == '\n' || c == '\r') {
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
                row.push_back(field);
                field.clear();
                if (!(row.size() == 1 && row[0].empty())) rows.push_back(row);
                row.clear();
            } else {
                field += c;
            }
        }
        auto out = std::make_shared<List>();
        for (const auto& r : rows) {
            auto lineList = std::make_shared<List>();
            for (const auto& f : r) lineList->items.push_back(vStr(f));
            out->items.push_back(vList(lineList));
        }
        return vList(out);
    });
    registerBuiltin("生成CSV", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto rows = needList(a[0], "生成CSV", line);
        std::ostringstream o;
        for (size_t r = 0; r < rows->items.size(); ++r) {
            auto row = needList(rows->items[r], "生成CSV", line);
            for (size_t c = 0; c < row->items.size(); ++c) {
                if (c) o << ",";
                string field = toText(row->items[c], false);
                bool needQuote = field.find_first_of(",\"\n\r") != string::npos;
                if (needQuote) {
                    o << '"';
                    for (char ch : field) {
                        if (ch == '"') o << "\"\"";
                        else o << ch;
                    }
                    o << '"';
                } else {
                    o << field;
                }
            }
            o << "\n";
        }
        return vStr(o.str());
    });

    // ---------- HTML 库 ----------
    registerBuiltin("去标签", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(htmlToText(needStr(a[0], "去标签", line)));
    });
    registerBuiltin("解码实体", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vStr(htmlDecodeEntities(needStr(a[0], "解码实体", line)));
    });
    registerBuiltin("取标签", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string html = needStr(a[0], "取标签", line);
        string tag = needStr(a[1], "取标签", line);
        auto l = std::make_shared<List>();
        for (const auto& s : htmlTagContents(html, tag)) l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("取属性", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string html = needStr(a[0], "取属性", line);
        string tag = needStr(a[1], "取属性", line);
        string attr = needStr(a[2], "取属性", line);
        auto l = std::make_shared<List>();
        for (const auto& s : htmlAttributeValues(html, tag, attr)) l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("取链接", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string html = needStr(a[0], "取链接", line);
        auto l = std::make_shared<List>();
        for (const auto& s : htmlAttributeValues(html, "a", "href")) l->items.push_back(vStr(s));
        return vList(l);
    });
    registerBuiltin("取标题", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string title = htmlTitleText(needStr(a[0], "取标题", line));
        if (title.empty()) return vNone();
        return vStr(title);
    });

    // ---------- 图形库 ----------
    registerBuiltin("圆面积", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆面积", line);
        return vNum(std::acos(-1.0) * r * r);
    });
    registerBuiltin("圆周长", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆周长", line);
        return vNum(2.0 * std::acos(-1.0) * r);
    });
    registerBuiltin("球体积", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "球体积", line);
        return vNum(4.0 / 3.0 * std::acos(-1.0) * r * r * r);
    });
    registerBuiltin("球表面积", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "球表面积", line);
        return vNum(4.0 * std::acos(-1.0) * r * r);
    });
    registerBuiltin("圆柱体积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆柱体积", line);
        double h = needNum(a[1], "圆柱体积", line);
        return vNum(std::acos(-1.0) * r * r * h);
    });
    registerBuiltin("圆锥体积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆锥体积", line);
        double h = needNum(a[1], "圆锥体积", line);
        return vNum(std::acos(-1.0) * r * r * h / 3.0);
    });
    registerBuiltin("长方形面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "长方形面积", line) * needNum(a[1], "长方形面积", line));
    });
    registerBuiltin("三角形面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "三角形面积", line) * needNum(a[1], "三角形面积", line) / 2.0);
    });
    registerBuiltin("梯形面积", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "梯形面积", line);
        double y = needNum(a[1], "梯形面积", line);
        double h = needNum(a[2], "梯形面积", line);
        return vNum((x + y) * h / 2.0);
    });
    registerBuiltin("长方体体积", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "长方体体积", line) *
                    needNum(a[1], "长方体体积", line) *
                    needNum(a[2], "长方体体积", line));
    });
    registerBuiltin("勾股斜边", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "勾股斜边", line);
        double y = needNum(a[1], "勾股斜边", line);
        return vNum(std::sqrt(x * x + y * y));
    });
    registerBuiltin("两点距离", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double dx = needNum(a[2], "两点距离", line) - needNum(a[0], "两点距离", line);
        double dy = needNum(a[3], "两点距离", line) - needNum(a[1], "两点距离", line);
        return vNum(std::sqrt(dx * dx + dy * dy));
    });
    registerBuiltin("扇形面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "扇形面积", line);
        double deg = needNum(a[1], "扇形面积", line);
        return vNum(std::acos(-1.0) * r * r * deg / 360.0);
    });

    // 曲面/立体按整体公式直接算，不做三角面近似。
    registerBuiltin("圆环面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double R = needNum(a[0], "圆环面积", line);
        double r = needNum(a[1], "圆环面积", line);
        return vNum(std::acos(-1.0) * (R * R - r * r));
    });
    registerBuiltin("圆柱表面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆柱表面积", line);
        double h = needNum(a[1], "圆柱表面积", line);
        double pi = std::acos(-1.0);
        return vNum(2.0 * pi * r * (r + h));
    });
    registerBuiltin("圆柱侧面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆柱侧面积", line);
        double h = needNum(a[1], "圆柱侧面积", line);
        return vNum(2.0 * std::acos(-1.0) * r * h);
    });
    registerBuiltin("圆锥表面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆锥表面积", line);
        double h = needNum(a[1], "圆锥表面积", line);
        double pi = std::acos(-1.0);
        double l = std::sqrt(r * r + h * h);
        return vNum(pi * r * (r + l));
    });
    registerBuiltin("圆锥侧面积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "圆锥侧面积", line);
        double h = needNum(a[1], "圆锥侧面积", line);
        double l = std::sqrt(r * r + h * h);
        return vNum(std::acos(-1.0) * r * l);
    });
    registerBuiltin("长方体表面积", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "长方体表面积", line);
        double y = needNum(a[1], "长方体表面积", line);
        double z = needNum(a[2], "长方体表面积", line);
        return vNum(2.0 * (x * y + y * z + x * z));
    });
    registerBuiltin("正方体表面积", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "正方体表面积", line);
        return vNum(6.0 * x * x);
    });
    // 统一入口：面积(形状, 参数…)
    registerBuiltin("面积", 2, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string shape = needStr(a[0], "面积", line);
        auto num = [&](size_t i) -> double {
            if (i >= a.size())
                throw WenError("运行时错误", line, 0, "「面积」参数不够：" + shape);
            return needNum(a[i], "面积", line);
        };
        const double pi = std::acos(-1.0);
        if (shape == "圆") return vNum(pi * num(1) * num(1));
        if (shape == "圆环") return vNum(pi * (num(1) * num(1) - num(2) * num(2)));
        if (shape == "长方形" || shape == "矩形") return vNum(num(1) * num(2));
        if (shape == "正方形") return vNum(num(1) * num(1));
        if (shape == "三角形") return vNum(num(1) * num(2) / 2.0);
        if (shape == "梯形") return vNum((num(1) + num(2)) * num(3) / 2.0);
        if (shape == "扇形") return vNum(pi * num(1) * num(1) * num(2) / 360.0);
        throw WenError("运行时错误", line, 0, "「面积」不认识的形状：" + shape);
    });
    // 统一入口：表面积(形状, 参数…)，直接整体公式。
    registerBuiltin("表面积", 2, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string shape = needStr(a[0], "表面积", line);
        auto num = [&](size_t i) -> double {
            if (i >= a.size())
                throw WenError("运行时错误", line, 0, "「表面积」参数不够：" + shape);
            return needNum(a[i], "表面积", line);
        };
        const double pi = std::acos(-1.0);
        if (shape == "球") return vNum(4.0 * pi * num(1) * num(1));
        if (shape == "圆柱") return vNum(2.0 * pi * num(1) * (num(1) + num(2)));
        if (shape == "圆锥") {
            double r = num(1), h = num(2);
            return vNum(pi * r * (r + std::sqrt(r * r + h * h)));
        }
        if (shape == "长方体") {
            double x = num(1), y = num(2), z = num(3);
            return vNum(2.0 * (x * y + y * z + x * z));
        }
        if (shape == "正方体") return vNum(6.0 * num(1) * num(1));
        throw WenError("运行时错误", line, 0, "「表面积」不认识的形状：" + shape);
    });
    // 统一入口：侧面积(形状, 参数…)
    registerBuiltin("侧面积", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string shape = needStr(a[0], "侧面积", line);
        auto num = [&](size_t i) -> double {
            if (i >= a.size())
                throw WenError("运行时错误", line, 0, "「侧面积」参数不够：" + shape);
            return needNum(a[i], "侧面积", line);
        };
        const double pi = std::acos(-1.0);
        if (shape == "圆柱") return vNum(2.0 * pi * num(1) * num(2));
        if (shape == "圆锥") {
            double r = num(1), h = num(2);
            return vNum(pi * r * std::sqrt(r * r + h * h));
        }
        throw WenError("运行时错误", line, 0, "「侧面积」不认识的形状：" + shape);
    });

    // ---------- 概率库 ----------
    registerBuiltin("概率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double good = needNum(a[0], "概率", line);
        double total = needNum(a[1], "概率", line);
        if (total <= 0 || good < 0 || good > total)
            throw WenError("运行时错误", line, 0, "概率要求 0 ≤ 有利数 ≤ 总数，总数大于 0");
        return vNum(good / total);
    });
    registerBuiltin("条件概率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double ab = needNum(a[0], "条件概率", line);
        double b = needNum(a[1], "条件概率", line);
        if (b <= 0) throw WenError("运行时错误", line, 0, "条件概率的分母必须大于 0");
        return vNum(ab / b);
    });
    registerBuiltin("独立概率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "独立概率", line) * needNum(a[1], "独立概率", line));
    });
    registerBuiltin("贝叶斯", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double prior = needNum(a[0], "贝叶斯", line);
        double like = needNum(a[1], "贝叶斯", line);
        double evidence = needNum(a[2], "贝叶斯", line);
        if (evidence <= 0) throw WenError("运行时错误", line, 0, "贝叶斯的证据概率必须大于 0");
        return vNum(prior * like / evidence);
    });
    registerBuiltin("期望", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto vals = needList(a[0], "期望", line);
        auto probs = needList(a[1], "期望", line);
        if (vals->items.empty() || vals->items.size() != probs->items.size())
            throw WenError("运行时错误", line, 0, "期望要求两个非空且等长的列表");
        double sum = 0;
        for (size_t i = 0; i < vals->items.size(); ++i)
            sum += needNum(vals->items[i], "期望", line) *
                   needNum(probs->items[i], "期望", line);
        return vNum(sum);
    });
    registerBuiltin("二项概率", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long n = needIndex(a[0], "二项概率", line);
        long long k = needIndex(a[1], "二项概率", line);
        double p = needNum(a[2], "二项概率", line);
        if (n < 0 || k < 0 || k > n || p < 0 || p > 1)
            throw WenError("运行时错误", line, 0, "二项概率要求 0 ≤ k ≤ n，0 ≤ p ≤ 1");
        return vNum(combCount(n, k) * std::pow(p, (double)k) *
                    std::pow(1.0 - p, (double)(n - k)));
    });
    registerBuiltin("泊松概率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long k = needIndex(a[0], "泊松概率", line);
        double lambda = needNum(a[1], "泊松概率", line);
        if (k < 0 || lambda <= 0)
            throw WenError("运行时错误", line, 0, "泊松概率要求 k ≥ 0，λ > 0");
        return vNum(std::exp(-lambda) * std::pow(lambda, (double)k) /
                    std::tgamma((double)k + 1.0));
    });
    registerBuiltin("几何概率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long k = needIndex(a[0], "几何概率", line);
        double p = needNum(a[1], "几何概率", line);
        if (k < 1 || p <= 0 || p > 1)
            throw WenError("运行时错误", line, 0, "几何概率要求 k ≥ 1，0 < p ≤ 1");
        return vNum(std::pow(1.0 - p, (double)(k - 1)) * p);
    });
    registerBuiltin("超几何概率", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        long long N = needIndex(a[0], "超几何概率", line);
        long long K = needIndex(a[1], "超几何概率", line);
        long long n = needIndex(a[2], "超几何概率", line);
        long long k = needIndex(a[3], "超几何概率", line);
        if (N <= 0 || K < 0 || K > N || n < 0 || n > N || k < 0 || k > n)
            throw WenError("运行时错误", line, 0, "超几何概率参数不合法");
        double all = combCount(N, n);
        if (all == 0) return vNum(0);
        return vNum(combCount(K, k) * combCount(N - K, n - k) / all);
    });
    registerBuiltin("正态密度", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "正态密度", line);
        double mu = needNum(a[1], "正态密度", line);
        double sigma = needNum(a[2], "正态密度", line);
        if (sigma <= 0) throw WenError("运行时错误", line, 0, "正态分布的标准差必须大于 0");
        double z = (x - mu) / sigma;
        return vNum(std::exp(-0.5 * z * z) /
                    (sigma * std::sqrt(2.0 * std::acos(-1.0))));
    });
    registerBuiltin("正态累积", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "正态累积", line);
        double mu = needNum(a[1], "正态累积", line);
        double sigma = needNum(a[2], "正态累积", line);
        if (sigma <= 0)
            throw WenError("运行时错误", line, 0, "正态分布的标准差必须大于 0");
        return vNum(0.5 * (1.0 + std::erf((x - mu) / (sigma * std::sqrt(2.0)))));
    });
    registerBuiltin("均匀概率", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double lo = needNum(a[0], "均匀概率", line);
        double hi = needNum(a[1], "均匀概率", line);
        double x = needNum(a[2], "均匀概率", line);
        if (hi <= lo) throw WenError("运行时错误", line, 0, "均匀分布要求上界大于下界");
        if (x < lo || x > hi) return vNum(0);
        return vNum(1.0 / (hi - lo));
    });
    registerBuiltin("指数概率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "指数概率", line);
        double lambda = needNum(a[1], "指数概率", line);
        if (lambda <= 0) throw WenError("运行时错误", line, 0, "指数分布的 λ 必须大于 0");
        if (x < 0) return vNum(0);
        return vNum(lambda * std::exp(-lambda * x));
    });
    registerBuiltin("归一化", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto w = needList(a[0], "归一化", line);
        auto nw = normalizedWeights(w, "归一化", line);
        auto out = std::make_shared<List>();
        for (double x : nw) out->items.push_back(vNum(x));
        return vList(out);
    });
    registerBuiltin("累积概率", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto w = needList(a[0], "累积概率", line);
        auto nw = normalizedWeights(w, "累积概率", line);
        auto out = std::make_shared<List>();
        double acc = 0;
        for (double x : nw) {
            acc += x;
            out->items.push_back(vNum(acc));
        }
        return vList(out);
    });
    registerBuiltin("赔率", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double p = needNum(a[0], "赔率", line);
        if (p < 0 || p >= 1)
            throw WenError("运行时错误", line, 0, "赔率要求 0 ≤ 概率 < 1");
        return vNum(p / (1.0 - p));
    });
    registerBuiltin("按权重分配", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double total = needNum(a[0], "按权重分配", line);
        auto w = needList(a[1], "按权重分配", line);
        auto nw = normalizedWeights(w, "按权重分配", line);
        auto out = std::make_shared<List>();
        for (double x : nw) out->items.push_back(vNum(total * x));
        return vList(out);
    });
    registerBuiltin("加权选择", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        auto vals = needList(a[0], "加权选择", line);
        auto w = needList(a[1], "加权选择", line);
        if (vals->items.empty() || vals->items.size() != w->items.size())
            throw WenError("运行时错误", line, 0, "加权选择要求两个非空且等长的列表");
        auto nw = normalizedWeights(w, "加权选择", line);
        std::discrete_distribution<size_t> dist(nw.begin(), nw.end());
        return vals->items[dist(rng)];
    });
    registerBuiltin("随机抽样", 2, 2, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "随机抽样", line);
        long long n = needIndex(a[1], "随机抽样", line);
        if (n < 0 || n > (long long)l->items.size())
            throw WenError("运行时错误", line, 0, "抽样数量不能超过列表长度");
        auto vals = shuffledValues(l->items, rng);
        auto out = std::make_shared<List>();
        for (long long i = 0; i < n; ++i) out->items.push_back(vals[(size_t)i]);
        return vList(out);
    });
    registerBuiltin("洗牌", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "洗牌", line);
        auto out = std::make_shared<List>();
        out->items = shuffledValues(l->items, rng);
        return vList(out);
    });

    registerBuiltin("新建三维面", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string color = a.size() > 1 ? needStr(a[1], "新建三维面", line) : "#4aa3ff";
        return make3Face(render3Vertices(a[0], line), color);
    });
    registerBuiltin("新建立方体", 4, 5, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "新建立方体", line);
        double y = needNum(a[1], "新建立方体", line);
        double z = needNum(a[2], "新建立方体", line);
        double s = needNum(a[3], "新建立方体", line);
        string color = a.size() > 4 ? needStr(a[4], "新建立方体", line) : "#4aa3ff";
        return make3Box(x, y, z, s, color);
    });
    registerBuiltin("新建球", 4, 6, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double x = needNum(a[0], "新建球", line);
        double y = needNum(a[1], "新建球", line);
        double z = needNum(a[2], "新建球", line);
        double r = needNum(a[3], "新建球", line);
        int segs = a.size() > 4 ? (int)needIndex(a[4], "新建球", line) : 12;
        string color = a.size() > 5 ? needStr(a[5], "新建球", line) : "#f0a040";
        return make3Sphere(x, y, z, r, segs, color);
    });
    registerBuiltin("渲染三维", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        vector<Value> faces;
        collect3Faces(a[0], faces, line);
        double cam = needNum(a[1], "渲染三维", line);
        if (cam <= 0.1) throw WenError("运行时错误", line, 0, "相机距离必须大于 0.1");
        string path = needStr(a[2], "渲染三维", line);
        struct Proj {
            vector<std::pair<double, double>> pts;
            double z = 0;
            string color;
        };
        vector<Proj> out;
        const double f = 520.0;
        for (const auto& fv : faces) {
            auto d = needDict(fv, "三维面", line);
            auto pts = render3Vertices(d->items.at("顶点"), line);
            string color = d->items.count("颜色") ? needStr(d->items["颜色"], "颜色", line)
                                                  : "#4aa3ff";
            Proj p;
            p.color = color;
            double zsum = 0;
            bool ok = true;
            for (const auto& v : pts) {
                double zz = v.z + cam;
                if (zz <= 0.1) { ok = false; break; }
                p.pts.push_back({400.0 + v.x * f / zz, 300.0 - v.y * f / zz});
                zsum += zz;
            }
            if (!ok) continue;
            p.z = zsum / (double)pts.size();
            out.push_back(p);
        }
        std::sort(out.begin(), out.end(), [](const Proj& x, const Proj& y) { return x.z > y.z; });
        std::ostringstream svg;
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"600\" "
               "viewBox=\"0 0 800 600\">\n<rect width=\"800\" height=\"600\" fill=\"#0e1218\"/>\n";
        for (const auto& p : out) {
            auto rgb = renderHexRgb(p.color);
            double lum = 0.55;
            svg << "<polygon points=\"";
            for (size_t i = 0; i < p.pts.size(); ++i) {
                if (i) svg << " ";
                svg << std::fixed << std::setprecision(2) << p.pts[i].first << ","
                    << p.pts[i].second;
            }
            svg << "\" fill=\"rgb(" << (int)(rgb[0] * 255 * lum) << ","
                << (int)(rgb[1] * 255 * lum) << "," << (int)(rgb[2] * 255 * lum)
                << ")\" stroke=\"#000\" stroke-width=\"0.8\"/>\n";
        }
        svg << "<text x=\"16\" y=\"28\" fill=\"#9fb3c8\" font-size=\"14\">"
               "文语言 · 3D 软件渲染</text>\n</svg>\n";
        writeRenderSvg(path, svg.str(), line);
        return vStr(path);
    });
    registerBuiltin("导出OBJ", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        vector<Value> faces;
        collect3Faces(a[0], faces, line);
        string path = needStr(a[1], "导出OBJ", line);
        std::ostringstream obj;
        size_t base = 1;
        for (const auto& fv : faces) {
            auto d = needDict(fv, "三维面", line);
            auto pts = render3Vertices(d->items.at("顶点"), line);
            for (const auto& v : pts)
                obj << "v " << v.x << " " << v.y << " " << v.z << "\n";
            obj << "f";
            for (size_t i = 0; i < pts.size(); ++i) obj << " " << base + i;
            obj << "\n";
            base += pts.size();
        }
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, obj.str(), false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });
    registerBuiltin("添加场景", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        std::function<void(const Value&)> add;
        add = [&](const Value& v) {
            if (v.type == VT::Dict) {
                renderScene.push_back(v);
            } else if (v.type == VT::List && v.list) {
                for (const auto& item : v.list->items) add(item);
            } else {
                throw WenError("运行时错误", line, 0, "添加场景需要面或面列表");
            }
        };
        add(a[0]);
        return vNum((double)renderScene.size());
    });
    // ---------- 渲染库 ----------
    registerBuiltin("新建面", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return makeFaceValue(renderVerticesFromList(a[0], line));
    });
    registerBuiltin("面面积", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto pts = renderFaceVertices(a[0], line);
        return vNum(renderPolygonArea(pts));
    });
    registerBuiltin("移动面", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto pts = renderFaceVertices(a[0], line);
        double dx = needNum(a[1], "移动面", line);
        double dy = needNum(a[2], "移动面", line);
        for (auto& p : pts) {
            p.x += dx;
            p.y += dy;
        }
        return makeFaceValue(pts);
    });
    registerBuiltin("旋转面", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto pts = renderFaceVertices(a[0], line);
        double deg = needNum(a[1], "旋转面", line);
        double cx = needNum(a[2], "旋转面", line);
        double cy = needNum(a[3], "旋转面", line);
        double rad = deg * std::acos(-1.0) / 180.0;
        double cs = std::cos(rad), sn = std::sin(rad);
        for (auto& p : pts) {
            double x = p.x - cx, y = p.y - cy;
            p.x = cx + x * cs - y * sn;
            p.y = cy + x * sn + y * cs;
        }
        return makeFaceValue(pts);
    });
    registerBuiltin("缩放面", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto pts = renderFaceVertices(a[0], line);
        double scale = needNum(a[1], "缩放面", line);
        double cx = needNum(a[2], "缩放面", line);
        double cy = needNum(a[3], "缩放面", line);
        for (auto& p : pts) {
            p.x = cx + (p.x - cx) * scale;
            p.y = cy + (p.y - cy) * scale;
        }
        return makeFaceValue(pts);
    });
    registerBuiltin("涂鸦", 4, 4, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "涂鸦", line);
        auto pts = renderFaceVertices(a[1], line);
        long long n = needIndex(a[2], "涂鸦", line);
        if (n < 1 || n > 20000)
            throw WenError("运行时错误", line, 0, "涂鸦数量要在 1 到 20000 之间");
        string pattern = needStr(a[3], "涂鸦", line);
        double area = renderPolygonArea(pts);
        double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
        for (const auto& p : pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        auto dots = renderDistribute(pts, (int)n, minx, miny, maxx, maxy, line);
        double w = (maxx - minx) + 80, h = (maxy - miny) + 110;
        auto tx = [&](double x) { return x - minx + 40; };
        auto ty = [&](double y) { return y - miny + 70; };
        vector<RenderPoint> tpts;
        for (const auto& p : pts) tpts.push_back({tx(p.x), ty(p.y)});
        double cell = std::sqrt(area / (double)n);
        double r = std::max(3.0, std::min(40.0, cell * 0.32));
        std::ostringstream o;
        o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int)w
          << "\" height=\"" << (int)h << "\" viewBox=\"0 0 " << (int)w << " " << (int)h
          << "\">\n";
        o << "<rect width=\"100%\" height=\"100%\" fill=\"#fbfbfb\"/>\n";
        o << "<defs><clipPath id=\"faceClip\"><polygon points=\""
          << renderPointsAttr(tpts) << "\"/></clipPath></defs>\n";
        o << "<polygon points=\"" << renderPointsAttr(tpts)
          << "\" fill=\"#ffffff\" stroke=\"#333333\" stroke-width=\"1.5\"/>\n";
        o << "<g clip-path=\"url(#faceClip)\">\n";
        for (const auto& p : dots) o << renderMotif(pattern, tx(p.x), ty(p.y), r);
        o << "</g>\n";
        o << "<text x=\"" << (int)(w / 2)
          << "\" y=\"30\" text-anchor=\"middle\" font-size=\"18\" "
             "font-family=\"Microsoft YaHei, sans-serif\" fill=\"#222\">涂鸦 "
          << n << " 画</text>\n";
        o << "</svg>\n";
        writeRenderSvg(path, o.str(), line);
        auto scene = std::make_shared<Dict>();
        scene->items["类型"] = vStr("涂鸦");
        scene->items["顶点"] = renderPointsValue(pts);
        scene->items["数量"] = vNum((double)n);
        scene->items["图案"] = vStr(pattern);
        vm.renderScene.push_back(vDict(scene));
        return vStr(path);
    });
    registerBuiltin("渐变", 5, 5, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "渐变", line);
        auto pts = renderFaceVertices(a[1], line);
        string c1 = renderColor(needStr(a[2], "渐变", line), "#ff8800");
        string c2 = renderColor(needStr(a[3], "渐变", line), "#0066ff");
        string dir = needStr(a[4], "渐变", line);
        double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
        for (const auto& p : pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        double w = (maxx - minx) + 80, h = (maxy - miny) + 110;
        auto tx = [&](double x) { return x - minx + 40; };
        auto ty = [&](double y) { return y - miny + 70; };
        vector<RenderPoint> tpts;
        for (const auto& p : pts) tpts.push_back({tx(p.x), ty(p.y)});
        string x2 = "1", y2 = "0";
        if (dir == "竖") { x2 = "0"; y2 = "1"; }
        else if (dir != "横") { x2 = "1"; y2 = "1"; }
        std::ostringstream o;
        o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int)w
          << "\" height=\"" << (int)h << "\" viewBox=\"0 0 " << (int)w << " " << (int)h
          << "\">\n";
        o << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
        o << "<defs><linearGradient id=\"grad\" x1=\"0\" y1=\"0\" x2=\"" << x2
          << "\" y2=\"" << y2 << "\">\n";
        o << "<stop offset=\"0\" stop-color=\"" << c1 << "\"/>\n";
        o << "<stop offset=\"1\" stop-color=\"" << c2 << "\"/>\n";
        o << "</linearGradient></defs>\n";
        o << "<polygon points=\"" << renderPointsAttr(tpts)
          << "\" fill=\"url(#grad)\" stroke=\"#333333\" stroke-width=\"1.5\"/>\n";
        o << "<text x=\"" << (int)(w / 2)
          << "\" y=\"30\" text-anchor=\"middle\" font-size=\"18\" "
             "font-family=\"Microsoft YaHei, sans-serif\" fill=\"#222\">渐变</text>\n";
        o << "</svg>\n";
        writeRenderSvg(path, o.str(), line);
        auto scene = std::make_shared<Dict>();
        scene->items["类型"] = vStr("渐变");
        scene->items["顶点"] = renderPointsValue(pts);
        scene->items["颜色1"] = vStr(c1);
        scene->items["颜色2"] = vStr(c2);
        scene->items["方向"] = vStr(dir);
        vm.renderScene.push_back(vDict(scene));
        return vStr(path);
    });
    registerBuiltin("体积光2D", 5, 5, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "体积光2D", line);
        auto pts = renderFaceVertices(a[1], line);
        double lx = needNum(a[2], "体积光2D", line);
        double ly = needNum(a[3], "体积光2D", line);
        double s = needNum(a[4], "体积光2D", line);
        s = std::max(0.0, std::min(1.0, s));
        double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
        for (const auto& p : pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        writeRenderSvg(path,
                       renderLightSvg(pts, lx, ly, s, false, "体积光 · 2D 形体雾",
                                      minx, miny, maxx, maxy),
                       line);
        auto scene = std::make_shared<Dict>();
        scene->items["类型"] = vStr("体积光2D");
        scene->items["顶点"] = renderPointsValue(pts);
        auto light = std::make_shared<List>();
        light->items.push_back(vNum(lx));
        light->items.push_back(vNum(ly));
        scene->items["光源"] = vList(light);
        scene->items["强度"] = vNum(s);
        vm.renderScene.push_back(vDict(scene));
        return vStr(path);
    });
    registerBuiltin("体积光3D", 5, 5, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "体积光3D", line);
        auto pts = renderFaceVertices(a[1], line);
        double lx = needNum(a[2], "体积光3D", line);
        double ly = needNum(a[3], "体积光3D", line);
        double s = needNum(a[4], "体积光3D", line);
        s = std::max(0.0, std::min(1.0, s));
        double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
        for (const auto& p : pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        writeRenderSvg(path,
                       renderLightSvg(pts, lx, ly, s, true, "体积光 · 3D 范围雾",
                                      minx, miny, maxx, maxy),
                       line);
        auto scene = std::make_shared<Dict>();
        scene->items["类型"] = vStr("体积光3D");
        scene->items["顶点"] = renderPointsValue(pts);
        auto light = std::make_shared<List>();
        light->items.push_back(vNum(lx));
        light->items.push_back(vNum(ly));
        scene->items["光源"] = vList(light);
        scene->items["强度"] = vNum(s);
        vm.renderScene.push_back(vDict(scene));
        return vStr(path);
    });

    registerBuiltin("场景", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        l->items = renderScene;
        return vList(l);
    });
    registerBuiltin("清空场景", 0, 0, [this](Interpreter&, vector<Value>&, int) -> Value {
        renderScene.clear();
        return vNone();
    });
    registerBuiltin("导出场景", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "导出场景", line);
        auto l = std::make_shared<List>();
        l->items = renderScene;
        string text = jsonDump(vList(l), line);
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, text, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });

    registerBuiltin("导出OpenGL", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "导出OpenGL", line);
        if (renderScene.empty())
            throw WenError("运行时错误", line, 0, "场景是空的；先调用涂鸦/渐变/体积光");
        bool has3D = false;
        for (const auto& node : renderScene) {
            auto d = needDict(node, "场景", line);
            auto t = d->items.find("类型");
            if (t != d->items.end() && toText(t->second) == "三维面") has3D = true;
        }
        if (has3D) {
            string tpl3;
            try {
                tpl3 = readWholeFile("渲染后端/opengl3d模板.cpp");
            } catch (IoError& e) {
                throw WenError("文件错误", line, 0,
                               "打不开 3D 模板（渲染后端/opengl3d模板.cpp）：" + string(e.what()));
            }
            std::ostringstream code;
            auto f3 = [](double v) {
                std::ostringstream s;
                s << std::setprecision(8) << v;
                string t = s.str();
                if (t.find('.') == string::npos && t.find('e') == string::npos &&
                    t.find('E') == string::npos) t += ".0";
                return t + "f";
            };
            double maxr = 1.0;
            for (const auto& node : renderScene) {
                auto d = needDict(node, "场景", line);
                auto t = d->items.find("类型");
                if (t == d->items.end() || toText(t->second) != "三维面") continue;
                auto pts = render3Vertices(d->items.at("顶点"), line);
                string color = d->items.count("颜色") ? needStr(d->items["颜色"], "颜色", line) : "#4aa3ff";
                auto rgb = renderHexRgb(color);
                code << "    {{";
                for (size_t i = 0; i < pts.size(); ++i) {
                    if (i) code << ", ";
                    code << "{" << f3(pts[i].x) << "," << f3(pts[i].y) << "," << f3(pts[i].z) << "}";
                    maxr = std::max(maxr, std::sqrt(pts[i].x * pts[i].x + pts[i].y * pts[i].y + pts[i].z * pts[i].z));
                }
                code << "}, " << f3(rgb[0]) << ", " << f3(rgb[1]) << ", " << f3(rgb[2]) << "},\n";
            }
            size_t p3 = tpl3.find("// __SCENE3D__");
            if (p3 == string::npos) throw WenError("引擎错误", line, 0, "3D 模板缺少 __SCENE3D__");
            tpl3.replace(p3, string("// __SCENE3D__").size(), code.str());
            auto repl = [&](const string& key, const string& val) {
                size_t q = tpl3.find(key);
                if (q != string::npos) tpl3.replace(q, key.size(), val);
            };
            repl("@W@", "900");
            repl("@H@", "650");
            repl("@CAM@", std::to_string(std::max(3.0, maxr * 2.8)));
            try {
                ensureParentDirectory(path);
                writeWholeFile(path, tpl3, false);
            } catch (IoError& e) {
                throw WenError("文件错误", line, 0, e.what());
            }
            return vStr(path);
        }
        string tpl;
        try {
            tpl = readWholeFile("渲染后端/opengl模板.cpp");
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0,
                           "打不开 OpenGL 模板（渲染后端/opengl模板.cpp）：" + string(e.what()));
        }
        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        for (const auto& node : renderScene) {
            auto pts = renderFaceVertices(node, line);
            for (const auto& p : pts) {
                minx = std::min(minx, p.x);
                maxx = std::max(maxx, p.x);
                miny = std::min(miny, p.y);
                maxy = std::max(maxy, p.y);
            }
        }
        const double margin = 40;
        int W = (int)std::max(320.0, (maxx - minx) + margin * 2);
        int H = (int)std::max(240.0, (maxy - miny) + margin * 2);
        auto sx = [&](double x) { return x - minx + margin; };
        auto sy = [&](double y) { return y - miny + margin; };
        std::ostringstream code;
        auto fnum = [](double v) {
            std::ostringstream s;
            s << std::setprecision(8) << v;
            string t = s.str();
            if (t.find('.') == string::npos && t.find('e') == string::npos &&
                t.find('E') == string::npos)
                t += ".0";
            return t + "f";
        };
        for (const auto& node : renderScene) {
            auto d = needDict(node, "场景", line);
            auto typeIt = d->items.find("类型");
            if (typeIt == d->items.end()) continue;
            string type = toText(typeIt->second);
            auto pts = renderFaceVertices(node, line);
            code << "    {\n      std::vector<std::array<float,2>> face = {";
            for (size_t i = 0; i < pts.size(); ++i) {
                if (i) code << ", ";
                code << "{" << fnum(sx(pts[i].x)) << "," << fnum(sy(pts[i].y)) << "}";
            }
            code << "};\n";
            if (type == "渐变") {
                auto c1 = renderHexRgb(needStr(d->items["颜色1"], "颜色1", line));
                auto c2 = renderHexRgb(needStr(d->items["颜色2"], "颜色2", line));
                string dir = needStr(d->items["方向"], "方向", line);
                int di = dir == "横" ? 0 : (dir == "竖" ? 1 : 2);
                code << "      drawGradient(face, " << fnum(c1[0]) << "," << fnum(c1[1]) << ","
                     << fnum(c1[2]) << ", " << fnum(c2[0]) << "," << fnum(c2[1]) << ","
                     << fnum(c2[2]) << ", " << di << ");\n";
            } else if (type == "体积光2D" || type == "体积光3D") {
                auto light = needList(d->items["光源"], "光源", line);
                double lx = needNum(light->items[0], "光源 x", line);
                double ly = needNum(light->items[1], "光源 y", line);
                double s = needNum(d->items["强度"], "强度", line);
                code << "      drawLight(face, " << fnum(sx(lx)) << "," << fnum(sy(ly)) << ","
                     << fnum(s) << ", " << (type == "体积光3D" ? "true" : "false") << ");\n";
            } else if (type == "涂鸦") {
                long long n = (long long)needNum(d->items["数量"], "数量", line);
                double bx1 = 1e300, by1 = 1e300, bx2 = -1e300, by2 = -1e300;
                for (const auto& p : pts) {
                    bx1 = std::min(bx1, p.x);
                    bx2 = std::max(bx2, p.x);
                    by1 = std::min(by1, p.y);
                    by2 = std::max(by2, p.y);
                }
                auto dots = renderDistribute(pts, (int)n, bx1, by1, bx2, by2, line);
                code << "      std::vector<std::array<float,2>> dots = {";
                for (size_t i = 0; i < dots.size(); ++i) {
                    if (i) code << ", ";
                    code << "{" << fnum(sx(dots[i].x)) << "," << fnum(sy(dots[i].y)) << "}";
                }
                code << "};\n      drawDoodle(face, dots);\n";
            }
            code << "    }\n";
        }
        size_t pos = tpl.find("// __SCENE__");
        if (pos == string::npos)
            throw WenError("引擎错误", line, 0, "OpenGL 模板缺少 __SCENE__ 标记");
        tpl.replace(pos, string("// __SCENE__").size(), code.str());
        size_t wpos = tpl.find("@W@");
        if (wpos != string::npos) tpl.replace(wpos, 3, std::to_string(W));
        size_t hpos = tpl.find("@H@");
        if (hpos != string::npos) tpl.replace(hpos, 3, std::to_string(H));
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, tpl, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });

    registerBuiltin("导出WebGPU", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "导出WebGPU", line);
        if (renderScene.empty())
            throw WenError("运行时错误", line, 0, "场景是空的；先调用涂鸦/渐变/体积光");
        string tpl;
        try {
            tpl = readWholeFile("渲染后端/webgpu模板.html");
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0,
                           "打不开 WebGPU 模板（渲染后端/webgpu模板.html）：" + string(e.what()));
        }
        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        for (const auto& node : renderScene) {
            auto pts = renderFaceVertices(node, line);
            for (const auto& p : pts) {
                minx = std::min(minx, p.x);
                maxx = std::max(maxx, p.x);
                miny = std::min(miny, p.y);
                maxy = std::max(maxy, p.y);
            }
        }
        const double margin = 40;
        int W = (int)std::max(320.0, (maxx - minx) + margin * 2);
        int H = (int)std::max(240.0, (maxy - miny) + margin * 2);
        auto nx = [&](double x) { return ((x - minx + margin) / W) * 2.0 - 1.0; };
        auto ny = [&](double y) { return 1.0 - ((y - miny + margin) / H) * 2.0; };
        auto jnum = [](double v) {
            std::ostringstream s;
            s << std::setprecision(8) << v;
            return s.str();
        };
        std::ostringstream data;
        bool first = true;
        auto tri = [&](double x1, double y1, std::array<float, 3> c1, double x2, double y2,
                       std::array<float, 3> c2, double x3, double y3, std::array<float, 3> c3) {
            auto put = [&](double x, double y, std::array<float, 3> c) {
                if (!first) data << ",";
                first = false;
                data << jnum(x) << "," << jnum(y) << "," << jnum(c[0]) << "," << jnum(c[1]) << ","
                     << jnum(c[2]) << ",1";
            };
            put(x1, y1, c1);
            put(x2, y2, c2);
            put(x3, y3, c3);
        };
        for (const auto& node : renderScene) {
            auto d = needDict(node, "场景", line);
            auto typeIt = d->items.find("类型");
            if (typeIt == d->items.end()) continue;
            string type = toText(typeIt->second);
            auto pts = renderFaceVertices(node, line);
            if (type == "渐变") {
                auto c1 = renderHexRgb(needStr(d->items["颜色1"], "颜色1", line));
                auto c2 = renderHexRgb(needStr(d->items["颜色2"], "颜色2", line));
                string dir = needStr(d->items["方向"], "方向", line);
                double bx1 = 1e300, by1 = 1e300, bx2 = -1e300, by2 = -1e300;
                for (const auto& p : pts) {
                    bx1 = std::min(bx1, p.x);
                    bx2 = std::max(bx2, p.x);
                    by1 = std::min(by1, p.y);
                    by2 = std::max(by2, p.y);
                }
                auto colorAt = [&](double x, double y) {
                    double tx = (bx2 > bx1) ? (x - bx1) / (bx2 - bx1) : 0;
                    double ty = (by2 > by1) ? (y - by1) / (by2 - by1) : 0;
                    double t = dir == "横" ? tx : (dir == "竖" ? ty : 0.5 * (tx + ty));
                    return std::array<float, 3>{
                        (float)(c1[0] + (c2[0] - c1[0]) * t),
                        (float)(c1[1] + (c2[1] - c1[1]) * t),
                        (float)(c1[2] + (c2[2] - c1[2]) * t)};
                };
                auto c0 = colorAt(pts[0].x, pts[0].y);
                for (size_t i = 1; i + 1 < pts.size(); ++i) {
                    auto ca = colorAt(pts[i].x, pts[i].y);
                    auto cb = colorAt(pts[i + 1].x, pts[i + 1].y);
                    tri(nx(pts[0].x), ny(pts[0].y), c0, nx(pts[i].x), ny(pts[i].y), ca,
                        nx(pts[i + 1].x), ny(pts[i + 1].y), cb);
                }
            } else if (type == "体积光2D" || type == "体积光3D") {
                auto light = needList(d->items["光源"], "光源", line);
                double lx = needNum(light->items[0], "光源 x", line);
                double ly = needNum(light->items[1], "光源 y", line);
                double s = needNum(d->items["强度"], "强度", line);
                bool vol = type == "体积光3D";
                auto lightAt = [&](double x, double y) {
                    double dist = std::sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly));
                    double scale = vol ? 220.0 : 120.0;
                    double g = s * std::exp(-dist / scale);
                    if (vol) g += 0.35 * s * std::exp(-dist / (scale * 2.2));
                    g = std::min(1.0, g);
                    return std::array<float, 3>{(float)(0.06 + g), (float)(0.09 + g),
                                                (float)(0.14 + g)};
                };
                auto c0 = lightAt(pts[0].x, pts[0].y);
                for (size_t i = 1; i + 1 < pts.size(); ++i) {
                    auto ca = lightAt(pts[i].x, pts[i].y);
                    auto cb = lightAt(pts[i + 1].x, pts[i + 1].y);
                    tri(nx(pts[0].x), ny(pts[0].y), c0, nx(pts[i].x), ny(pts[i].y), ca,
                        nx(pts[i + 1].x), ny(pts[i + 1].y), cb);
                }
            } else if (type == "涂鸦") {
                long long n = (long long)needNum(d->items["数量"], "数量", line);
                double bx1 = 1e300, by1 = 1e300, bx2 = -1e300, by2 = -1e300;
                for (const auto& p : pts) {
                    bx1 = std::min(bx1, p.x);
                    bx2 = std::max(bx2, p.x);
                    by1 = std::min(by1, p.y);
                    by2 = std::max(by2, p.y);
                }
                auto dots = renderDistribute(pts, (int)n, bx1, by1, bx2, by2, line);
                std::array<float, 3> white{0.98f, 0.98f, 0.98f};
                auto c0 = white;
                for (size_t i = 1; i + 1 < pts.size(); ++i)
                    tri(nx(pts[0].x), ny(pts[0].y), c0, nx(pts[i].x), ny(pts[i].y), white,
                        nx(pts[i + 1].x), ny(pts[i + 1].y), white);
                for (const auto& dot : dots) {
                    const int seg = 16;
                    const double r = 4.0;
                    for (int k = 0; k < seg; ++k) {
                        double a1 = k * 2 * std::acos(-1.0) / seg;
                        double a2 = (k + 1) * 2 * std::acos(-1.0) / seg;
                        tri(nx(dot.x), ny(dot.y), {0.15f, 0.15f, 0.15f},
                            nx(dot.x + std::cos(a1) * r), ny(dot.y + std::sin(a1) * r),
                            {0.15f, 0.15f, 0.15f},
                            nx(dot.x + std::cos(a2) * r), ny(dot.y + std::sin(a2) * r),
                            {0.15f, 0.15f, 0.15f});
                    }
                }
            }
        }
        size_t dpos = tpl.find("@DATA@");
        if (dpos == string::npos)
            throw WenError("引擎错误", line, 0, "WebGPU 模板缺少 @DATA@ 标记");
        tpl.replace(dpos, 6, data.str());
        tpl.replace(tpl.find("@W@"), 3, std::to_string(W));
        tpl.replace(tpl.find("@H@"), 3, std::to_string(H));
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, tpl, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });

    registerBuiltin("导出Vulkan", 1, 1, [this](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "导出Vulkan", line);
        if (renderScene.empty())
            throw WenError("运行时错误", line, 0, "场景是空的；先调用涂鸦/渐变/体积光");
        string tpl;
        try {
            tpl = readWholeFile("渲染后端/vulkan模板.cpp");
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0,
                           "打不开 Vulkan 模板（渲染后端/vulkan模板.cpp）：" + string(e.what()));
        }
        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        for (const auto& node : renderScene) {
            auto pts = renderFaceVertices(node, line);
            for (const auto& p : pts) {
                minx = std::min(minx, p.x);
                maxx = std::max(maxx, p.x);
                miny = std::min(miny, p.y);
                maxy = std::max(maxy, p.y);
            }
        }
        const double margin = 40;
        int W = (int)std::max(320.0, (maxx - minx) + margin * 2);
        int H = (int)std::max(240.0, (maxy - miny) + margin * 2);
        auto nx = [&](double x) { return ((x - minx + margin) / W) * 2.0 - 1.0; };
        auto ny = [&](double y) { return 1.0 - ((y - miny + margin) / H) * 2.0; };
        auto jnum = [](double v) {
            std::ostringstream s;
            s << std::setprecision(8) << v;
            return s.str();
        };
        std::ostringstream data;
        bool first = true;
        auto tri = [&](double x1, double y1, std::array<float, 3> c1, double x2, double y2,
                       std::array<float, 3> c2, double x3, double y3, std::array<float, 3> c3) {
            auto put = [&](double x, double y, std::array<float, 3> c) {
                if (!first) data << ",";
                first = false;
                data << "{" << cppFloatLit(x) << "f," << cppFloatLit(y) << "f,"
                     << cppFloatLit(c[0]) << "f," << cppFloatLit(c[1]) << "f,"
                     << cppFloatLit(c[2]) << "f,1.0f}";
            };
            put(x1, y1, c1);
            put(x2, y2, c2);
            put(x3, y3, c3);
        };
        for (const auto& node : renderScene) {
            auto d = needDict(node, "场景", line);
            auto typeIt = d->items.find("类型");
            if (typeIt == d->items.end()) continue;
            string type = toText(typeIt->second);
            auto pts = renderFaceVertices(node, line);
            if (type == "渐变") {
                auto c1 = renderHexRgb(needStr(d->items["颜色1"], "颜色1", line));
                auto c2 = renderHexRgb(needStr(d->items["颜色2"], "颜色2", line));
                string dir = needStr(d->items["方向"], "方向", line);
                double bx1 = 1e300, by1 = 1e300, bx2 = -1e300, by2 = -1e300;
                for (const auto& p : pts) {
                    bx1 = std::min(bx1, p.x);
                    bx2 = std::max(bx2, p.x);
                    by1 = std::min(by1, p.y);
                    by2 = std::max(by2, p.y);
                }
                auto colorAt = [&](double x, double y) {
                    double tx = (bx2 > bx1) ? (x - bx1) / (bx2 - bx1) : 0;
                    double ty = (by2 > by1) ? (y - by1) / (by2 - by1) : 0;
                    double t = dir == "横" ? tx : (dir == "竖" ? ty : 0.5 * (tx + ty));
                    return std::array<float, 3>{
                        (float)(c1[0] + (c2[0] - c1[0]) * t),
                        (float)(c1[1] + (c2[1] - c1[1]) * t),
                        (float)(c1[2] + (c2[2] - c1[2]) * t)};
                };
                auto c0 = colorAt(pts[0].x, pts[0].y);
                for (size_t i = 1; i + 1 < pts.size(); ++i) {
                    auto ca = colorAt(pts[i].x, pts[i].y);
                    auto cb = colorAt(pts[i + 1].x, pts[i + 1].y);
                    tri(nx(pts[0].x), ny(pts[0].y), c0, nx(pts[i].x), ny(pts[i].y), ca,
                        nx(pts[i + 1].x), ny(pts[i + 1].y), cb);
                }
            } else if (type == "体积光2D" || type == "体积光3D") {
                auto light = needList(d->items["光源"], "光源", line);
                double lx = needNum(light->items[0], "光源 x", line);
                double ly = needNum(light->items[1], "光源 y", line);
                double s = needNum(d->items["强度"], "强度", line);
                bool vol = type == "体积光3D";
                auto lightAt = [&](double x, double y) {
                    double dist = std::sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly));
                    double scale = vol ? 220.0 : 120.0;
                    double g = s * std::exp(-dist / scale);
                    if (vol) g += 0.35 * s * std::exp(-dist / (scale * 2.2));
                    g = std::min(1.0, g);
                    return std::array<float, 3>{(float)(0.06 + g), (float)(0.09 + g),
                                                (float)(0.14 + g)};
                };
                auto c0 = lightAt(pts[0].x, pts[0].y);
                for (size_t i = 1; i + 1 < pts.size(); ++i) {
                    auto ca = lightAt(pts[i].x, pts[i].y);
                    auto cb = lightAt(pts[i + 1].x, pts[i + 1].y);
                    tri(nx(pts[0].x), ny(pts[0].y), c0, nx(pts[i].x), ny(pts[i].y), ca,
                        nx(pts[i + 1].x), ny(pts[i + 1].y), cb);
                }
            } else if (type == "涂鸦") {
                long long n = (long long)needNum(d->items["数量"], "数量", line);
                double bx1 = 1e300, by1 = 1e300, bx2 = -1e300, by2 = -1e300;
                for (const auto& p : pts) {
                    bx1 = std::min(bx1, p.x);
                    bx2 = std::max(bx2, p.x);
                    by1 = std::min(by1, p.y);
                    by2 = std::max(by2, p.y);
                }
                auto dots = renderDistribute(pts, (int)n, bx1, by1, bx2, by2, line);
                for (size_t i = 1; i + 1 < pts.size(); ++i)
                    tri(nx(pts[0].x), ny(pts[0].y), {0.98f, 0.98f, 0.98f}, nx(pts[i].x),
                        ny(pts[i].y), {0.98f, 0.98f, 0.98f}, nx(pts[i + 1].x), ny(pts[i + 1].y),
                        {0.98f, 0.98f, 0.98f});
                for (const auto& dot : dots) {
                    const int seg = 16;
                    const double r = 4.0;
                    for (int k = 0; k < seg; ++k) {
                        double a1 = k * 2 * std::acos(-1.0) / seg;
                        double a2 = (k + 1) * 2 * std::acos(-1.0) / seg;
                        tri(nx(dot.x), ny(dot.y), {0.15f, 0.15f, 0.15f},
                            nx(dot.x + std::cos(a1) * r), ny(dot.y + std::sin(a1) * r),
                            {0.15f, 0.15f, 0.15f},
                            nx(dot.x + std::cos(a2) * r), ny(dot.y + std::sin(a2) * r),
                            {0.15f, 0.15f, 0.15f});
                    }
                }
            }
        }
        size_t dpos = tpl.find("@DATA@");
        if (dpos == string::npos)
            throw WenError("引擎错误", line, 0, "Vulkan 模板缺少 @DATA@ 标记");
        tpl.replace(dpos, 6, data.str());
        tpl.replace(tpl.find("@W@"), 3, std::to_string(W));
        tpl.replace(tpl.find("@H@"), 3, std::to_string(H));
        try {
            ensureParentDirectory(path);
            writeWholeFile(path, tpl, false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        // 着色器放到同目录，构建脚本会编译成 .spv。
        size_t slash = path.find_last_of("\\/");
        string dir = slash == string::npos ? string() : path.substr(0, slash + 1);
        try {
            writeWholeFile(dir + "scene.vert", readWholeFile("渲染后端/scene.vert"), false);
            writeWholeFile(dir + "scene.frag", readWholeFile("渲染后端/scene.frag"), false);
        } catch (IoError& e) {
            throw WenError("文件错误", line, 0, e.what());
        }
        return vStr(path);
    });

    // ---------- 物理库 ----------
    registerBuiltin("速度", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double d = needNum(a[0], "速度", line), t = needNum(a[1], "速度", line);
        if (t == 0) throw WenError("运行时错误", line, 0, "时间不能为 0");
        return vNum(d / t);
    });
    registerBuiltin("加速度", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double dv = needNum(a[0], "加速度", line), t = needNum(a[1], "加速度", line);
        if (t == 0) throw WenError("运行时错误", line, 0, "时间不能为 0");
        return vNum(dv / t);
    });
    registerBuiltin("力", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "力", line) * needNum(a[1], "力", line));
    });
    registerBuiltin("功", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "功", line) * needNum(a[1], "功", line));
    });
    registerBuiltin("功率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double w = needNum(a[0], "功率", line), t = needNum(a[1], "功率", line);
        if (t == 0) throw WenError("运行时错误", line, 0, "时间不能为 0");
        return vNum(w / t);
    });
    registerBuiltin("动能", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "动能", line), v = needNum(a[1], "动能", line);
        return vNum(0.5 * m * v * v);
    });
    registerBuiltin("势能", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "势能", line), h = needNum(a[1], "势能", line);
        double g = a.size() == 3 ? needNum(a[2], "势能", line) : 9.80665;
        return vNum(m * g * h);
    });
    registerBuiltin("动量", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "动量", line) * needNum(a[1], "动量", line));
    });
    registerBuiltin("密度", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "密度", line), v = needNum(a[1], "密度", line);
        if (v == 0) throw WenError("运行时错误", line, 0, "体积不能为 0");
        return vNum(m / v);
    });
    registerBuiltin("压强", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double f = needNum(a[0], "压强", line), s = needNum(a[1], "压强", line);
        if (s == 0) throw WenError("运行时错误", line, 0, "面积不能为 0");
        return vNum(f / s);
    });
    registerBuiltin("浮力", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double rho = needNum(a[0], "浮力", line), v = needNum(a[1], "浮力", line);
        double g = a.size() == 3 ? needNum(a[2], "浮力", line) : 9.80665;
        return vNum(rho * v * g);
    });
    registerBuiltin("电压", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "电压", line) * needNum(a[1], "电压", line));
    });
    registerBuiltin("电流", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double u = needNum(a[0], "电流", line), r = needNum(a[1], "电流", line);
        if (r == 0) throw WenError("运行时错误", line, 0, "电阻不能为 0");
        return vNum(u / r);
    });
    registerBuiltin("电阻", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double u = needNum(a[0], "电阻", line), i = needNum(a[1], "电阻", line);
        if (i == 0) throw WenError("运行时错误", line, 0, "电流不能为 0");
        return vNum(u / i);
    });
    registerBuiltin("波速", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "波速", line) * needNum(a[1], "波速", line));
    });
    registerBuiltin("周期", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double f = needNum(a[0], "周期", line);
        if (f == 0) throw WenError("运行时错误", line, 0, "频率不能为 0");
        return vNum(1.0 / f);
    });
    registerBuiltin("频率", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double t = needNum(a[0], "频率", line);
        if (t == 0) throw WenError("运行时错误", line, 0, "周期不能为 0");
        return vNum(1.0 / t);
    });
    registerBuiltin("自由落体时间", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double h = needNum(a[0], "自由落体时间", line);
        double g = a.size() == 2 ? needNum(a[1], "自由落体时间", line) : 9.80665;
        if (h < 0 || g <= 0)
            throw WenError("运行时错误", line, 0, "高度不能为负，重力加速度必须大于 0");
        return vNum(std::sqrt(2.0 * h / g));
    });
    registerBuiltin("自由落体速度", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double t = needNum(a[0], "自由落体速度", line);
        double g = a.size() == 2 ? needNum(a[1], "自由落体速度", line) : 9.80665;
        return vNum(g * t);
    });
    registerBuiltin("向心加速度", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double v = needNum(a[0], "向心加速度", line), r = needNum(a[1], "向心加速度", line);
        if (r == 0) throw WenError("运行时错误", line, 0, "半径不能为 0");
        return vNum(v * v / r);
    });
    registerBuiltin("万有引力", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m1 = needNum(a[0], "万有引力", line);
        double m2 = needNum(a[1], "万有引力", line);
        double r = needNum(a[2], "万有引力", line);
        if (r == 0) throw WenError("运行时错误", line, 0, "距离不能为 0");
        return vNum(6.67430e-11 * m1 * m2 / (r * r));
    });
    registerBuiltin("摄氏转华氏", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "摄氏转华氏", line) * 9.0 / 5.0 + 32.0);
    });
    registerBuiltin("华氏转摄氏", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum((needNum(a[0], "华氏转摄氏", line) - 32.0) * 5.0 / 9.0);
    });
    registerBuiltin("摄氏转开尔文", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "摄氏转开尔文", line) + 273.15);
    });
    registerBuiltin("开尔文转摄氏", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "开尔文转摄氏", line) - 273.15);
    });

    registerBuiltin("体积", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "体积", line);
        double rho = needNum(a[1], "体积", line);
        if (rho == 0) throw WenError("运行时错误", line, 0, "密度不能为 0");
        return vNum(m / rho);
    });
    registerBuiltin("重力", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "重力", line);
        double g = a.size() == 2 ? needNum(a[1], "重力", line) : 9.80665;
        return vNum(m * g);
    });
    registerBuiltin("滑动摩擦力", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "滑动摩擦力", line) * needNum(a[1], "滑动摩擦力", line));
    });
    registerBuiltin("弹力", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "弹力", line) * needNum(a[1], "弹力", line));
    });
    registerBuiltin("向心力", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "向心力", line);
        double v = needNum(a[1], "向心力", line);
        double r = needNum(a[2], "向心力", line);
        if (r == 0) throw WenError("运行时错误", line, 0, "半径不能为 0");
        return vNum(m * v * v / r);
    });
    registerBuiltin("角速度", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double T = needNum(a[0], "角速度", line);
        if (T == 0) throw WenError("运行时错误", line, 0, "周期不能为 0");
        return vNum(2.0 * std::acos(-1.0) / T);
    });
    registerBuiltin("线速度", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double r = needNum(a[0], "线速度", line);
        double T = needNum(a[1], "线速度", line);
        if (T == 0) throw WenError("运行时错误", line, 0, "周期不能为 0");
        return vNum(2.0 * std::acos(-1.0) * r / T);
    });
    registerBuiltin("单摆周期", 1, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double L = needNum(a[0], "单摆周期", line);
        double g = a.size() == 2 ? needNum(a[1], "单摆周期", line) : 9.80665;
        if (L < 0 || g <= 0)
            throw WenError("运行时错误", line, 0, "摆长不能为负，重力加速度必须大于 0");
        return vNum(2.0 * std::acos(-1.0) * std::sqrt(L / g));
    });
    registerBuiltin("弹性势能", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double k = needNum(a[0], "弹性势能", line);
        double x = needNum(a[1], "弹性势能", line);
        return vNum(0.5 * k * x * x);
    });
    registerBuiltin("机械效率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double useful = needNum(a[0], "机械效率", line);
        double total = needNum(a[1], "机械效率", line);
        if (total == 0) throw WenError("运行时错误", line, 0, "总功不能为 0");
        return vNum(useful / total * 100.0);
    });
    registerBuiltin("杠杆阻力", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double F1 = needNum(a[0], "杠杆阻力", line);
        double L1 = needNum(a[1], "杠杆阻力", line);
        double L2 = needNum(a[2], "杠杆阻力", line);
        if (L2 == 0) throw WenError("运行时错误", line, 0, "阻力臂不能为 0");
        return vNum(F1 * L1 / L2);
    });
    registerBuiltin("液体压强", 2, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double rho = needNum(a[0], "液体压强", line);
        double h = needNum(a[1], "液体压强", line);
        double g = a.size() == 3 ? needNum(a[2], "液体压强", line) : 9.80665;
        return vNum(rho * g * h);
    });
    registerBuiltin("流量", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "流量", line) * needNum(a[1], "流量", line));
    });
    registerBuiltin("热量", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "热量", line) * needNum(a[1], "热量", line) *
                    needNum(a[2], "热量", line));
    });
    registerBuiltin("热值放热", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "热值放热", line) * needNum(a[1], "热值放热", line));
    });
    registerBuiltin("热效率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double useful = needNum(a[0], "热效率", line);
        double total = needNum(a[1], "热效率", line);
        if (total == 0) throw WenError("运行时错误", line, 0, "总热量不能为 0");
        return vNum(useful / total * 100.0);
    });
    registerBuiltin("热膨胀伸长", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "热膨胀伸长", line) *
                    needNum(a[1], "热膨胀伸长", line) *
                    needNum(a[2], "热膨胀伸长", line));
    });
    registerBuiltin("理想气体压强", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double n = needNum(a[0], "理想气体压强", line);
        double T = needNum(a[1], "理想气体压强", line);
        double V = needNum(a[2], "理想气体压强", line);
        if (V == 0) throw WenError("运行时错误", line, 0, "体积不能为 0");
        return vNum(n * 8.314462618 * T / V);
    });
    registerBuiltin("理想气体体积", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double n = needNum(a[0], "理想气体体积", line);
        double T = needNum(a[1], "理想气体体积", line);
        double p = needNum(a[2], "理想气体体积", line);
        if (p == 0) throw WenError("运行时错误", line, 0, "压强不能为 0");
        return vNum(n * 8.314462618 * T / p);
    });
    registerBuiltin("电功率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "电功率", line) * needNum(a[1], "电功率", line));
    });
    registerBuiltin("电能", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "电能", line) * needNum(a[1], "电能", line));
    });
    registerBuiltin("焦耳热", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double I = needNum(a[0], "焦耳热", line);
        double R = needNum(a[1], "焦耳热", line);
        double t = needNum(a[2], "焦耳热", line);
        return vNum(I * I * R * t);
    });
    registerBuiltin("串联电阻", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "串联电阻", line) + needNum(a[1], "串联电阻", line));
    });
    registerBuiltin("并联电阻", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double R1 = needNum(a[0], "并联电阻", line);
        double R2 = needNum(a[1], "并联电阻", line);
        if (R1 + R2 == 0)
            throw WenError("运行时错误", line, 0, "两个电阻不能同时为 0");
        return vNum(R1 * R2 / (R1 + R2));
    });
    registerBuiltin("电场力", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "电场力", line) * needNum(a[1], "电场力", line));
    });
    registerBuiltin("电势能", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(needNum(a[0], "电势能", line) * needNum(a[1], "电势能", line));
    });
    registerBuiltin("折射率", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double v = needNum(a[0], "折射率", line);
        if (v <= 0) throw WenError("运行时错误", line, 0, "介质中的光速必须大于 0");
        return vNum(299792458.0 / v);
    });
    registerBuiltin("折射定律", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double theta1 = needNum(a[0], "折射定律", line);
        double n1 = needNum(a[1], "折射定律", line);
        double n2 = needNum(a[2], "折射定律", line);
        if (n2 == 0) throw WenError("运行时错误", line, 0, "折射率不能为 0");
        double s = n1 / n2 * std::sin(theta1 * std::acos(-1.0) / 180.0);
        if (s < -1 || s > 1)
            throw WenError("运行时错误", line, 0, "发生全反射，没有折射角");
        return vNum(std::asin(s) * 180.0 / std::acos(-1.0));
    });
    registerBuiltin("临界角", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double n = needNum(a[0], "临界角", line);
        if (n <= 1) throw WenError("运行时错误", line, 0, "折射率必须大于 1");
        return vNum(std::asin(1.0 / n) * 180.0 / std::acos(-1.0));
    });
    registerBuiltin("透镜像距", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double u = needNum(a[0], "透镜像距", line);
        double f = needNum(a[1], "透镜像距", line);
        if (u == 0 || f == 0)
            throw WenError("运行时错误", line, 0, "物距和焦距不能为 0");
        double inv = 1.0 / f - 1.0 / u;
        if (inv == 0) throw WenError("运行时错误", line, 0, "像距无穷大");
        return vNum(1.0 / inv);
    });
    registerBuiltin("透镜放大率", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double v = needNum(a[0], "透镜放大率", line);
        double u = needNum(a[1], "透镜放大率", line);
        if (u == 0) throw WenError("运行时错误", line, 0, "物距不能为 0");
        return vNum(std::fabs(v / u));
    });
    registerBuiltin("光子能量", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        return vNum(6.62607015e-34 * needNum(a[0], "光子能量", line));
    });
    registerBuiltin("质能", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "质能", line);
        return vNum(m * 299792458.0 * 299792458.0);
    });
    registerBuiltin("德布罗意波长", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double m = needNum(a[0], "德布罗意波长", line);
        double v = needNum(a[1], "德布罗意波长", line);
        if (m == 0 || v == 0)
            throw WenError("运行时错误", line, 0, "质量和速度不能为 0");
        return vNum(6.62607015e-34 / (m * v));
    });
    registerBuiltin("第一宇宙速度", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double M = needNum(a[0], "第一宇宙速度", line);
        double r = needNum(a[1], "第一宇宙速度", line);
        if (r <= 0) throw WenError("运行时错误", line, 0, "半径必须大于 0");
        return vNum(std::sqrt(6.67430e-11 * M / r));
    });
    registerBuiltin("轨道周期", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        double M = needNum(a[0], "轨道周期", line);
        double r = needNum(a[1], "轨道周期", line);
        if (M <= 0 || r <= 0)
            throw WenError("运行时错误", line, 0, "中心质量和轨道半径必须大于 0");
        return vNum(2.0 * std::acos(-1.0) * std::sqrt(r * r * r / (6.67430e-11 * M)));
    });

    // ---------- 函数库 ----------
    registerBuiltin("映射", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "映射", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「映射」的第二个参数必须是函数");
        auto out = std::make_shared<List>();
        for (const auto& item : l->items) {
            vector<Value> args{item};
            out->items.push_back(vm.callValue(a[1], args, line));
        }
        return vList(out);
    });
    registerBuiltin("过滤", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "过滤", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「过滤」的第二个参数必须是函数");
        auto out = std::make_shared<List>();
        for (const auto& item : l->items) {
            vector<Value> args{item};
            if (truthy(vm.callValue(a[1], args, line))) out->items.push_back(item);
        }
        return vList(out);
    });
    registerBuiltin("折叠", 3, 3, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "折叠", line);
        if (a[2].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「折叠」的第三个参数必须是函数");
        Value acc = a[1];
        for (const auto& item : l->items) {
            vector<Value> args{acc, item};
            acc = vm.callValue(a[2], args, line);
        }
        return acc;
    });
    registerBuiltin("每个", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "每个", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「每个」的第二个参数必须是函数");
        for (const auto& item : l->items) {
            vector<Value> args{item};
            vm.callValue(a[1], args, line);
        }
        return vNone();
    });
    registerBuiltin("复合", 3, 3, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        if (a[0].type != VT::Func || a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「复合」的前两个参数必须是函数");
        vector<Value> innerArgs{a[2]};
        Value inner = vm.callValue(a[1], innerArgs, line);
        vector<Value> outerArgs{inner};
        return vm.callValue(a[0], outerArgs, line);
    });
    registerBuiltin("迭代", 3, 3, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        if (a[0].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「迭代」的第一个参数必须是函数");
        long long n = needIndex(a[2], "迭代", line);
        if (n < 0) throw WenError("运行时错误", line, 0, "迭代次数不能为负");
        Value cur = a[1];
        for (long long i = 0; i < n; ++i) {
            vector<Value> args{cur};
            cur = vm.callValue(a[0], args, line);
        }
        return cur;
    });
    registerBuiltin("斜率", 2, 3, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        if (a[0].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「斜率」的第一个参数必须是函数");
        double x = needNum(a[1], "斜率", line);
        double h = a.size() == 3 ? needNum(a[2], "斜率", line) : 1e-5;
        if (h == 0) throw WenError("运行时错误", line, 0, "步长不能为 0");
        vector<Value> x1{vNum(x + h)}, x2{vNum(x - h)};
        double y1 = needNum(vm.callValue(a[0], x1, line), "斜率", line);
        double y2 = needNum(vm.callValue(a[0], x2, line), "斜率", line);
        return vNum((y1 - y2) / (2.0 * h));
    });
    registerBuiltin("定积分", 3, 4, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        if (a[0].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「定积分」的第一个参数必须是函数");
        double lo = needNum(a[1], "定积分", line);
        double hi = needNum(a[2], "定积分", line);
        int n = a.size() == 4 ? (int)needIndex(a[3], "定积分", line) : 1000;
        if (n < 2) n = 2;
        if (n % 2) ++n;
        double h = (hi - lo) / n;
        auto f = [&](double x) {
            vector<Value> args{vNum(x)};
            return needNum(vm.callValue(a[0], args, line), "定积分", line);
        };
        double sum = f(lo) + f(hi);
        for (int i = 1; i < n; i += 2) sum += 4.0 * f(lo + i * h);
        for (int i = 2; i < n; i += 2) sum += 2.0 * f(lo + i * h);
        return vNum(sum * h / 3.0);
    });

    registerBuiltin("任意", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "任意", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「任意」的第二个参数必须是函数");
        for (const auto& item : l->items) {
            vector<Value> args{item};
            if (truthy(vm.callValue(a[1], args, line))) return vBool(true);
        }
        return vBool(false);
    });
    registerBuiltin("全部", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "全部", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「全部」的第二个参数必须是函数");
        for (const auto& item : l->items) {
            vector<Value> args{item};
            if (!truthy(vm.callValue(a[1], args, line))) return vBool(false);
        }
        return vBool(true);
    });
    registerBuiltin("找到", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "找到", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「找到」的第二个参数必须是函数");
        for (const auto& item : l->items) {
            vector<Value> args{item};
            if (truthy(vm.callValue(a[1], args, line))) return item;
        }
        return vNone();
    });
    registerBuiltin("计数", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "计数", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「计数」的第二个参数必须是函数");
        long long n = 0;
        for (const auto& item : l->items) {
            vector<Value> args{item};
            if (truthy(vm.callValue(a[1], args, line))) ++n;
        }
        return vNum((double)n);
    });
    registerBuiltin("排序依据", 2, 3, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "排序依据", line);
        if (a[1].type != VT::Func)
            throw WenError("运行时错误", line, 0, "「排序依据」的第二个参数必须是函数");
        bool reverse = a.size() == 3 && truthy(a[2]);
        vector<Value> items = l->items;
        auto keyOf = [&](const Value& v) {
            vector<Value> args{v};
            return vm.callValue(a[1], args, line);
        };
        std::stable_sort(items.begin(), items.end(), [&](const Value& x, const Value& y) {
            Value kx = keyOf(x), ky = keyOf(y);
            if (kx.type == VT::Num && ky.type == VT::Num) return kx.num < ky.num;
            return toText(kx) < toText(ky);
        });
        if (reverse) std::reverse(items.begin(), items.end());
        auto out = std::make_shared<List>();
        out->items = std::move(items);
        return vList(out);
    });
    registerBuiltin("去重", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "去重", line);
        auto out = std::make_shared<List>();
        for (const auto& item : l->items) {
            bool exists = false;
            for (const auto& k : out->items)
                if (valueEquals(k, item)) {
                    exists = true;
                    break;
                }
            if (!exists) out->items.push_back(item);
        }
        return vList(out);
    });
    registerBuiltin("展平", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "展平", line);
        auto out = std::make_shared<List>();
        for (const auto& item : l->items) {
            if ((item.type == VT::List || item.type == VT::Set) && item.list)
                for (const auto& inner : item.list->items) out->items.push_back(inner);
            else
                out->items.push_back(item);
        }
        return vList(out);
    });

    // ---------- 集合库 ----------
    registerBuiltin("新建集合", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        return vSet();
    });
    registerBuiltin("集合添加", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto s = needSet(a[0], "集合添加", line);
        for (const auto& item : s->items)
            if (valueEquals(item, a[1])) return a[0];
        s->items.push_back(a[1]);
        return a[0];
    });
    registerBuiltin("集合删除", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto s = needSet(a[0], "集合删除", line);
        for (auto it = s->items.begin(); it != s->items.end(); ++it) {
            if (valueEquals(*it, a[1])) { s->items.erase(it); break; }
        }
        return a[0];
    });
    registerBuiltin("集合存在", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto s = needSet(a[0], "集合存在", line);
        for (const auto& item : s->items)
            if (valueEquals(item, a[1])) return vBool(true);
        return vBool(false);
    });
    registerBuiltin("并集", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto x = needSet(a[0], "并集", line);
        auto y = needSet(a[1], "并集", line);
        auto out = std::make_shared<List>();
        for (const auto& item : x->items) out->items.push_back(item);
        for (const auto& item : y->items) {
            bool found = false;
            for (const auto& old : out->items)
                if (valueEquals(old, item)) { found = true; break; }
            if (!found) out->items.push_back(item);
        }
        return vSet(out);
    });
    registerBuiltin("交集", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto x = needSet(a[0], "交集", line);
        auto y = needSet(a[1], "交集", line);
        auto out = std::make_shared<List>();
        for (const auto& item : x->items) {
            for (const auto& other : y->items) {
                if (valueEquals(item, other)) {
                    out->items.push_back(item);
                    break;
                }
            }
        }
        return vSet(out);
    });
    registerBuiltin("差集", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto x = needSet(a[0], "差集", line);
        auto y = needSet(a[1], "差集", line);
        auto out = std::make_shared<List>();
        for (const auto& item : x->items) {
            bool found = false;
            for (const auto& other : y->items)
                if (valueEquals(item, other)) { found = true; break; }
            if (!found) out->items.push_back(item);
        }
        return vSet(out);
    });
    registerBuiltin("子集", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto x = needSet(a[0], "子集", line);
        auto y = needSet(a[1], "子集", line);
        for (const auto& item : x->items) {
            bool found = false;
            for (const auto& other : y->items)
                if (valueEquals(item, other)) { found = true; break; }
            if (!found) return vBool(false);
        }
        return vBool(true);
    });
    registerBuiltin("对称差", 2, 2, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto x = needSet(a[0], "对称差", line);
        auto y = needSet(a[1], "对称差", line);
        auto out = std::make_shared<List>();
        auto addUnique = [&](const Value& item) {
            for (const auto& old : out->items)
                if (valueEquals(old, item)) return;
            out->items.push_back(item);
        };
        for (const auto& item : x->items) {
            bool found = false;
            for (const auto& other : y->items)
                if (valueEquals(item, other)) { found = true; break; }
            if (!found) addUnique(item);
        }
        for (const auto& item : y->items) {
            bool found = false;
            for (const auto& other : x->items)
                if (valueEquals(item, other)) { found = true; break; }
            if (!found) addUnique(item);
        }
        return vSet(out);
    });

    // ---------- 点阵库 ----------
    registerBuiltin("新建点阵", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
        return vList();
    });
    registerBuiltin("点阵添加", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "点阵添加", line);
        double x = needNum(a[1], "点阵添加", line);
        double y = needNum(a[2], "点阵添加", line);
        auto p = std::make_shared<List>();
        p->items.push_back(vNum(x));
        p->items.push_back(vNum(y));
        l->items.push_back(vList(p));
        return a[0];
    });
    registerBuiltin("点阵移除", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "点阵移除", line);
        double x = needNum(a[1], "点阵移除", line);
        double y = needNum(a[2], "点阵移除", line);
        for (auto it = l->items.begin(); it != l->items.end(); ++it) {
            if (isPointAt(*it, x, y)) { l->items.erase(it); break; }
        }
        return a[0];
    });
    registerBuiltin("点阵包含", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "点阵包含", line);
        double x = needNum(a[1], "点阵包含", line);
        double y = needNum(a[2], "点阵包含", line);
        for (const auto& item : l->items)
            if (isPointAt(item, x, y)) return vBool(true);
        return vBool(false);
    });
    registerBuiltin("点阵数量", 1, 1, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "点阵数量", line);
        return vNum((double)l->items.size());
    });
    registerBuiltin("点阵平移", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "点阵平移", line);
        double dx = needNum(a[1], "点阵平移", line);
        double dy = needNum(a[2], "点阵平移", line);
        for (auto& item : l->items) {
            auto p = needList(item, "点阵平移", line);
            if (p->items.size() != 2) continue;
            p->items[0] = vNum(needNum(p->items[0], "点阵平移", line) + dx);
            p->items[1] = vNum(needNum(p->items[1], "点阵平移", line) + dy);
        }
        return a[0];
    });
    registerBuiltin("点阵缩放", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        auto l = needList(a[0], "点阵缩放", line);
        double scale = needNum(a[1], "点阵缩放", line);
        double cx = needNum(a[2], "点阵缩放", line);
        double cy = needNum(a[3], "点阵缩放", line);
        for (auto& item : l->items) {
            auto p = needList(item, "点阵缩放", line);
            if (p->items.size() != 2) continue;
            double x = needNum(p->items[0], "点阵缩放", line);
            double y = needNum(p->items[1], "点阵缩放", line);
            p->items[0] = vNum(cx + (x - cx) * scale);
            p->items[1] = vNum(cy + (y - cy) * scale);
        }
        return a[0];
    });

    // ---------- 频道库 ----------
    registerBuiltin("创建频道", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "创建频道", line);
        vm.channels[name];
        return vBool(true);
    });
    registerBuiltin("发送消息", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "发送消息", line);
        vm.channels[name].push_back(a[1]);
        return vBool(true);
    });
    registerBuiltin("读取消息", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "读取消息", line);
        auto it = vm.channels.find(name);
        if (it == vm.channels.end() || it->second.empty()) return vNone();
        Value v = it->second.front();
        it->second.pop_front();
        return v;
    });
    registerBuiltin("窥视消息", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "窥视消息", line);
        auto it = vm.channels.find(name);
        if (it == vm.channels.end() || it->second.empty()) return vNone();
        return it->second.front();
    });
    registerBuiltin("频道长度", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "频道长度", line);
        auto it = vm.channels.find(name);
        return vNum(it == vm.channels.end() ? 0.0 : (double)it->second.size());
    });
    registerBuiltin("清空频道", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "清空频道", line);
        vm.channels[name].clear();
        return vBool(true);
    });
    registerBuiltin("频道全部", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "频道全部", line);
        auto out = std::make_shared<List>();
        auto it = vm.channels.find(name);
        if (it != vm.channels.end())
            for (const auto& v : it->second) out->items.push_back(v);
        return vList(out);
    });

    // ---------- 输入库 ----------
    registerBuiltin("接收原始输入", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string text = needStr(a[0], "接收原始输入", line);
        for (const auto& ch : utf8Chars(text)) vm.inputBuffer.push_back(ch);
        return vBool(true);
    });
    registerBuiltin("取输入", 0, 0, [](Interpreter& vm, vector<Value>&, int) -> Value {
        if (vm.inputBuffer.empty()) return vStr("");
        string ch = vm.inputBuffer.front();
        vm.inputBuffer.pop_front();
        return vStr(ch);
    });
    registerBuiltin("预览输入", 0, 0, [](Interpreter& vm, vector<Value>&, int) -> Value {
        if (vm.inputBuffer.empty()) return vStr("");
        return vStr(vm.inputBuffer.front());
    });
    registerBuiltin("清空输入", 0, 0, [](Interpreter& vm, vector<Value>&, int) -> Value {
        vm.inputBuffer.clear();
        return vBool(true);
    });
    registerBuiltin("读键", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
#ifdef _WIN32
        // 不是控制台（管道/重定向）时按字节读，方便脚本测试。
        if (_isatty(_fileno(stdin)) == 0) {
            int g = std::getchar();
            if (g == EOF) return vNone();
            if (g == '\r') return vStr("回车");
            if (g == '\n') return vStr("回车");
            return vStr(string(1, (char)g));
        }
        int c = _getwch();
        if (c == -1) return vNone();
        if (c == 0 || c == 0xE0) {
            int c2 = _getwch();
            switch (c2) {
                case 72: return vStr("上");
                case 80: return vStr("下");
                case 75: return vStr("左");
                case 77: return vStr("右");
                case 71: return vStr("Home");
                case 79: return vStr("End");
                case 73: return vStr("上页");
                case 81: return vStr("下页");
                case 83: return vStr("删除");
                default: return vStr("键" + std::to_string(c2));
            }
        }
        if (c == 13) return vStr("回车");
        if (c == 27) return vStr("退出");
        if (c == 8) return vStr("退格");
        if (c == 9) return vStr("制表");
        if (c == 32) return vStr("空格");
        return vStr(wideToUtf8(std::wstring(1, (wchar_t)c)));
#else
        return vNone();
#endif
    });
    registerBuiltin("清屏", 0, 0, [](Interpreter&, vector<Value>&, int) -> Value {
#ifdef _WIN32
        std::system("cls");
#else
        std::cout << "\033[2J\033[H" << std::flush;
#endif
        return vNone();
    });

    // ---------- 注入库 ----------
    registerBuiltin("注入输入事件", 0, 0, [](Interpreter& vm, vector<Value>&, int) -> Value {
        if (vm.inputBuffer.empty()) return vStr("");
        string ch = vm.inputBuffer.front();
        vm.inputBuffer.pop_front();
        vm.channels["输入事件频道"].push_back(vStr(ch));
        return vStr(ch);
    });
    registerBuiltin("注入变量", 2, 2, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "注入变量", line);
        vm.globals->vars[name] = a[1];
        return a[1];
    });
    registerBuiltin("读取变量", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "读取变量", line);
        auto it = vm.globals->vars.find(name);
        if (it == vm.globals->vars.end())
            throw WenError("运行时错误", line, 0, "变量「" + name + "」还没有值");
        return it->second;
    });
    registerBuiltin("变量名单", 0, 0, [](Interpreter& vm, vector<Value>&, int) -> Value {
        auto l = std::make_shared<List>();
        for (const auto& kv : vm.globals->vars) l->items.push_back(vStr(kv.first));
        return vList(l);
    });
    registerBuiltin("变量存在", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "变量存在", line);
        return vBool(vm.globals->vars.count(name) > 0);
    });
    registerBuiltin("删除变量", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string name = needStr(a[0], "删除变量", line);
        return vBool(vm.globals->vars.erase(name) > 0);
    });
    registerBuiltin("执行文本", 1, 1, [](Interpreter& vm, vector<Value>& a, int line) -> Value {
        string src = needStr(a[0], "执行文本", line);
        vm.filename = "<注入>";
        vm.sourceLines = splitLines(src);
        try {
            auto program = vm.parseSource(src);
            vm.execProgram(program);
        } catch (IncompleteError& e) {
            throw WenError("语法错误", e.line, 0, e.what());
        }
        return vNone();
    });

    // ---------- 可视化库 ----------
    registerBuiltin("折线图", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "折线图", line);
        auto data = needList(a[1], "折线图", line);
        string title = needStr(a[2], "折线图", line);
        if (data->items.empty())
            throw WenError("运行时错误", line, 0, "折线图的数据不能为空");
        double mn = needNum(data->items[0], "折线图", line);
        double mx = mn;
        for (const auto& v : data->items) {
            double x = needNum(v, "折线图", line);
            mn = std::min(mn, x);
            mx = std::max(mx, x);
        }
        if (mx == mn) { mx += 1; mn -= 1; }
        const int W = 800, H = 500, L = 70, R = 30, T = 60, B = 60;
        double pw = W - L - R, ph = H - T - B;
        auto X = [&](size_t i) {
            if (data->items.size() <= 1) return L + pw / 2;
            return L + pw * (double)i / (double)(data->items.size() - 1);
        };
        auto Y = [&](double v) { return T + ph * (1.0 - (v - mn) / (mx - mn)); };
        std::ostringstream o;
        o << svgStart(title, W, H);
        o << "<line x1=\"" << L << "\" y1=\"" << T + ph << "\" x2=\"" << L + pw
          << "\" y2=\"" << T + ph << "\" stroke=\"#888\"/>\n";
        o << "<line x1=\"" << L << "\" y1=\"" << T << "\" x2=\"" << L << "\" y2=\""
          << T + ph << "\" stroke=\"#888\"/>\n";
        o << "<text x=\"" << L - 10 << "\" y=\"" << T + ph
          << "\" text-anchor=\"end\" font-size=\"12\" fill=\"#666\">"
          << numberToString(mn) << "</text>\n";
        o << "<text x=\"" << L - 10 << "\" y=\"" << T + 5
          << "\" text-anchor=\"end\" font-size=\"12\" fill=\"#666\">"
          << numberToString(mx) << "</text>\n";
        o << "<polyline fill=\"none\" stroke=\"#2563eb\" stroke-width=\"3\" points=\"";
        for (size_t i = 0; i < data->items.size(); ++i) {
            double v = needNum(data->items[i], "折线图", line);
            o << X(i) << "," << Y(v) << " ";
        }
        o << "\"/>\n";
        for (size_t i = 0; i < data->items.size(); ++i) {
            double v = needNum(data->items[i], "折线图", line);
            o << "<circle cx=\"" << X(i) << "\" cy=\"" << Y(v)
              << "\" r=\"3.5\" fill=\"#2563eb\"/>\n";
        }
        o << svgEnd();
        ensureParentDirectory(path);
        writeWholeFile(path, o.str(), false);
        return vBool(true);
    });

    registerBuiltin("柱状图", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "柱状图", line);
        auto data = needList(a[1], "柱状图", line);
        string title = needStr(a[2], "柱状图", line);
        if (data->items.empty())
            throw WenError("运行时错误", line, 0, "柱状图的数据不能为空");
        double mn = 0, mx = 0;
        for (size_t i = 0; i < data->items.size(); ++i) {
            double v = needNum(data->items[i], "柱状图", line);
            if (i == 0) { mn = std::min(0.0, v); mx = std::max(0.0, v); }
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        if (mx == mn) { mx += 1; mn -= 1; }
        const int W = 800, H = 500, L = 70, R = 30, T = 60, B = 60;
        double pw = W - L - R, ph = H - T - B;
        auto Y = [&](double v) { return T + ph * (1.0 - (v - mn) / (mx - mn)); };
        double zeroY = Y(0);
        double step = pw / (double)data->items.size();
        std::ostringstream o;
        o << svgStart(title, W, H);
        o << "<line x1=\"" << L << "\" y1=\"" << zeroY << "\" x2=\"" << L + pw
          << "\" y2=\"" << zeroY << "\" stroke=\"#888\"/>\n";
        for (size_t i = 0; i < data->items.size(); ++i) {
            double v = needNum(data->items[i], "柱状图", line);
            double x = L + i * step + step * 0.2;
            double w = step * 0.6;
            double y1 = std::min(zeroY, Y(v));
            double y2 = std::max(zeroY, Y(v));
            o << "<rect x=\"" << x << "\" y=\"" << y1 << "\" width=\"" << w
              << "\" height=\"" << (y2 - y1)
              << "\" fill=\"#16a34a\" opacity=\"0.85\"/>\n";
            o << "<text x=\"" << x + w / 2 << "\" y=\"" << y1 - 4
              << "\" text-anchor=\"middle\" font-size=\"11\" fill=\"#333\">"
              << numberToString(v) << "</text>\n";
        }
        o << svgEnd();
        ensureParentDirectory(path);
        writeWholeFile(path, o.str(), false);
        return vBool(true);
    });

    registerBuiltin("饼图", 4, 4, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "饼图", line);
        auto data = needList(a[1], "饼图", line);
        auto labels = needList(a[2], "饼图", line);
        string title = needStr(a[3], "饼图", line);
        if (data->items.empty() || data->items.size() != labels->items.size())
            throw WenError("运行时错误", line, 0, "饼图要求数据与标签非空且等长");
        double sum = 0;
        for (const auto& v : data->items) {
            double x = needNum(v, "饼图", line);
            if (x < 0) throw WenError("运行时错误", line, 0, "饼图的数据不能为负");
            sum += x;
        }
        if (sum <= 0) throw WenError("运行时错误", line, 0, "饼图的数据总和必须大于 0");
        const int W = 800, H = 500;
        const double cx = 330, cy = 280, r = 170;
        const char* palette[] = {"#2563eb", "#16a34a", "#f59e0b", "#dc2626",
                                 "#7c3aed", "#0891b2", "#db2777", "#65a30d"};
        std::ostringstream o;
        o << svgStart(title, W, H);
        double start = -std::acos(-1.0) / 2.0;
        for (size_t i = 0; i < data->items.size(); ++i) {
            double v = needNum(data->items[i], "饼图", line);
            double ang = 2.0 * std::acos(-1.0) * v / sum;
            double end = start + ang;
            double x1 = cx + r * std::cos(start), y1 = cy + r * std::sin(start);
            double x2 = cx + r * std::cos(end), y2 = cy + r * std::sin(end);
            int large = ang > std::acos(-1.0) ? 1 : 0;
            o << "<path d=\"M " << cx << " " << cy << " L " << x1 << " " << y1
              << " A " << r << " " << r << " 0 " << large << " 1 " << x2 << " " << y2
              << " Z\" fill=\"" << palette[i % 8] << "\" stroke=\"#fff\" stroke-width=\"2\"/>\n";
            double mid = start + ang / 2.0;
            double lx = cx + r * 0.65 * std::cos(mid);
            double ly = cy + r * 0.65 * std::sin(mid);
            o << "<text x=\"" << lx << "\" y=\"" << ly
              << "\" text-anchor=\"middle\" font-size=\"13\" fill=\"#fff\">"
              << numberToString(v / sum * 100) << "%</text>\n";
            double legY = 110 + i * 28;
            o << "<rect x=\"570\" y=\"" << legY - 12 << "\" width=\"16\" height=\"16\" fill=\""
              << palette[i % 8] << "\"/>\n";
            o << "<text x=\"594\" y=\"" << legY << "\" font-size=\"14\" fill=\"#222\">"
              << xmlEscape(needStr(labels->items[i], "饼图标签", line)) << "</text>\n";
            start = end;
        }
        o << svgEnd();
        ensureParentDirectory(path);
        writeWholeFile(path, o.str(), false);
        return vBool(true);
    });

    registerBuiltin("散点图", 3, 3, [](Interpreter&, vector<Value>& a, int line) -> Value {
        string path = needStr(a[0], "散点图", line);
        auto pts = needList(a[1], "散点图", line);
        string title = needStr(a[2], "散点图", line);
        if (pts->items.empty())
            throw WenError("运行时错误", line, 0, "散点图的数据不能为空");
        double xmn = 0, xmx = 0, ymn = 0, ymx = 0;
        for (size_t i = 0; i < pts->items.size(); ++i) {
            auto p = needList(pts->items[i], "散点图", line);
            if (p->items.size() != 2)
                throw WenError("运行时错误", line, 0, "散点必须是 [x, y]");
            double x = needNum(p->items[0], "散点图", line);
            double y = needNum(p->items[1], "散点图", line);
            if (i == 0) { xmn = xmx = x; ymn = ymx = y; }
            xmn = std::min(xmn, x); xmx = std::max(xmx, x);
            ymn = std::min(ymn, y); ymx = std::max(ymx, y);
        }
        if (xmx == xmn) { xmx += 1; xmn -= 1; }
        if (ymx == ymn) { ymx += 1; ymn -= 1; }
        const int W = 800, H = 500, L = 70, R = 30, T = 60, B = 60;
        double pw = W - L - R, ph = H - T - B;
        auto X = [&](double x) { return L + pw * (x - xmn) / (xmx - xmn); };
        auto Y = [&](double y) { return T + ph * (1.0 - (y - ymn) / (ymx - ymn)); };
        std::ostringstream o;
        o << svgStart(title, W, H);
        o << "<line x1=\"" << L << "\" y1=\"" << T + ph << "\" x2=\"" << L + pw
          << "\" y2=\"" << T + ph << "\" stroke=\"#888\"/>\n";
        o << "<line x1=\"" << L << "\" y1=\"" << T << "\" x2=\"" << L << "\" y2=\""
          << T + ph << "\" stroke=\"#888\"/>\n";
        for (const auto& item : pts->items) {
            auto p = needList(item, "散点图", line);
            double x = needNum(p->items[0], "散点图", line);
            double y = needNum(p->items[1], "散点图", line);
            o << "<circle cx=\"" << X(x) << "\" cy=\"" << Y(y)
              << "\" r=\"5\" fill=\"#dc2626\" opacity=\"0.8\"/>\n";
        }
        o << svgEnd();
        ensureParentDirectory(path);
        writeWholeFile(path, o.str(), false);
        return vBool(true);
    });

    // 核心只保留“调用 + 存写”：库函数先按库分表，等脚本写「依赖」时再装进全局。
    for (const auto& kv : functionModules()) {
        auto it = globals->vars.find(kv.first);
        if (it == globals->vars.end()) continue;
        libraryFunctions[kv.second][kv.first] = it->second;
        globals->vars.erase(it);
    }
}

// ============================================================
// 11. 中文 -> C++ 编译器（数值/数学快路径）
// ============================================================

struct CompileError : std::runtime_error {
    int line;
    CompileError(int line_, string msg)
        : std::runtime_error(std::move(msg)), line(line_) {}
};

static string cppQuote(const string& s) {
    string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    out += "\"";
    return out;
}

static string cppNumber(double d) {
    if (std::isnan(d) || std::isinf(d)) return "0.0";
    std::ostringstream oss;
    oss << std::setprecision(17) << d;
    string s = oss.str();
    if (s.find('.') == string::npos && s.find('e') == string::npos &&
        s.find('E') == string::npos) {
        s += ".0";
    }
    return s;
}

static const std::set<string>& cppReservedNames() {
    static const std::set<string> names = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
        "compl", "const", "constexpr", "const_cast", "continue", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
        // 生成代码里的 helper 名，避免变量把它们盖住
        "fmt", "fmtb", "safe_div", "safe_floor_div", "safe_mod", "safe_pow",
        "safe_sqrt", "safe_log", "safe_log10", "safe_logb", "sign", "rand_real",
        "rand_int", "rng", "fail", "main", "std"
    };
    return names;
}

static string cppVarName(const string& n) {
    if (cppReservedNames().count(n)) return "变量_" + n;
    if (n.rfind("函数_", 0) == 0) return "变量_" + n;
    return n;
}

static string cppFuncName(const string& n) { return "函数_" + n; }

static string cppPreamble() {
    return R"CPP(// 文语言（wen）· WEN-LANG-0.1
#include <bits/stdc++.h>
using namespace std;

static string fmt(double d) {
    if (isnan(d)) return "非数";
    if (isinf(d)) return d < 0 ? "负无穷" : "无穷";
    if (d == 0) return "0";
    double r = round(d);
    if (fabs(d - r) < 1e-12 && fabs(d) < 1e15) return to_string((long long)r);
    ostringstream o;
    o << setprecision(16) << defaultfloat << d;
    return o.str();
}
static string fmtb(bool b) { return b ? "真" : "假"; }
static mt19937 rng((unsigned)chrono::steady_clock::now().time_since_epoch().count());
static double rand_real() {
    uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng);
}
static double rand_int(double a, double b) {
    long long x = (long long)a, y = (long long)b;
    if (x > y) swap(x, y);
    uniform_int_distribution<long long> d(x, y);
    return (double)d(rng);
}
static void fail(int line, const string& m) {
    cerr << "运行时错误：第 " << line << " 行：" << m << "\n";
    exit(1);
}
static double safe_div(double a, double b, int line) {
    if (b == 0) fail(line, "除数不能为 0");
    return a / b;
}
static double safe_floor_div(double a, double b, int line) {
    if (b == 0) fail(line, "除数不能为 0");
    return floor(a / b);
}
static double safe_mod(double a, double b, int line) {
    if (b == 0) fail(line, "除数不能为 0");
    return a - floor(a / b) * b;
}
static double safe_pow(double a, double b, int line) {
    double r = pow(a, b);
    if (isnan(r)) fail(line, "幂运算的结果不是实数");
    return r;
}
static double safe_sqrt(double x, int line) {
    if (x < 0) fail(line, "负数不能开平方根");
    return sqrt(x);
}
static double safe_log(double x, int line) {
    if (x <= 0) fail(line, "对数的真数必须大于 0");
    return log(x);
}
static double safe_log10(double x, int line) {
    if (x <= 0) fail(line, "对数的真数必须大于 0");
    return log10(x);
}
static double safe_logb(double x, double b, int line) {
    if (x <= 0) fail(line, "对数的真数必须大于 0");
    if (b <= 0 || b == 1) fail(line, "对数的底不合法");
    return log(x) / log(b);
}
static double sign(double x) { return (x > 0) - (x < 0); }

)CPP";
}

class CppGenerator {
public:
    explicit CppGenerator(const vector<StmtPtr>& program) : program_(program) {
        for (const auto& s : program_) {
            if (s->k == Stmt::K::Import) {
                for (const auto& n : s->imports) imports_.insert(n);
            }
        }
        for (const auto& s : program_) {
            if (s->k == Stmt::K::Func) collectFunc(s);
        }
        for (const auto& s : program_) {
            if (s->k != Stmt::K::Func && s->k != Stmt::K::Import) {
                collectTop(s, true);
            }
        }
    }

    string generate();

private:
    struct FunctionInfo {
        string cppName;
        StmtPtr stmt;
        vector<string> params;
        map<string, string> localCpp;
        set<string> locals;
    };

    const vector<StmtPtr>& program_;
    map<string, string> globals_;
    map<string, shared_ptr<FunctionInfo>> funcs_;
    set<string> imports_;
    const FunctionInfo* cur_ = nullptr;
    int temp_ = 0;

    [[noreturn]] void err(int line, const string& msg) {
        throw CompileError(line, msg);
    }

    void collectTop(const StmtPtr& s, bool top) {
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k != Expr::K::Var)
                    err(s->line, "编译模式只支持给变量赋值，不支持列表/字典下标赋值");
                if (!globals_.count(s->target->text))
                    globals_[s->target->text] = cppVarName(s->target->text);
                break;
            case Stmt::K::If:
                for (const auto& br : s->branches)
                    for (const auto& st : br.second) collectTop(st, false);
                for (const auto& st : s->elseBody) collectTop(st, false);
                break;
            case Stmt::K::While:
                for (const auto& st : s->body) collectTop(st, false);
                break;
            case Stmt::K::For:
                if (!globals_.count(s->name))
                    globals_[s->name] = cppVarName(s->name);
                for (const auto& st : s->body) collectTop(st, false);
                break;
            case Stmt::K::Func:
                if (!top) err(s->line, "编译模式要求函数定义写在顶层");
                break;
            case Stmt::K::Return:
            case Stmt::K::Break:
            case Stmt::K::Continue:
                err(s->line, "这个语句不能写在顶层");
                break;
            case Stmt::K::Export:
                err(s->line, "「导出」只能写在库里");
                break;
            default:
                break;
        }
    }

    void addLocal(FunctionInfo& fi, const string& name) {
        if (fi.localCpp.count(name)) return;
        fi.locals.insert(name);
        fi.localCpp[name] = cppVarName(name);
    }

    void collectLocal(const StmtPtr& s, FunctionInfo& fi) {
        switch (s->k) {
            case Stmt::K::Assign:
                if (s->target->k != Expr::K::Var)
                    err(s->line, "编译模式只支持给变量赋值");
                addLocal(fi, s->target->text);
                break;
            case Stmt::K::If:
                for (const auto& br : s->branches)
                    for (const auto& st : br.second) collectLocal(st, fi);
                for (const auto& st : s->elseBody) collectLocal(st, fi);
                break;
            case Stmt::K::While:
                for (const auto& st : s->body) collectLocal(st, fi);
                break;
            case Stmt::K::For:
                addLocal(fi, s->name);
                for (const auto& st : s->body) collectLocal(st, fi);
                break;
            case Stmt::K::Func:
                err(s->line, "编译模式暂不支持嵌套函数");
                break;
            case Stmt::K::Export:
                err(s->line, "「导出」只能写在库里");
                break;
            default:
                break;
        }
    }

    void collectFunc(const StmtPtr& s) {
        if (funcs_.count(s->name))
            err(s->line, "函数「" + s->name + "」重复定义");
        auto fi = std::make_shared<FunctionInfo>();
        fi->cppName = cppFuncName(s->name);
        fi->stmt = s;
        fi->params = s->params;
        set<string> seen;
        for (size_t i = 0; i < s->params.size(); ++i) {
            if (!seen.insert(s->params[i]).second)
                err(s->line, "函数「" + s->name + "」的参数名重复");
            fi->localCpp[s->params[i]] = cppVarName(s->params[i]);
        }
        for (const auto& st : s->body) collectLocal(st, *fi);
        funcs_[s->name] = fi;
    }

    string varRef(const string& name, int line) {
        if (cur_) {
            auto it = cur_->localCpp.find(name);
            if (it != cur_->localCpp.end()) return it->second;
        }
        auto git = globals_.find(name);
        if (git != globals_.end()) return git->second;
        if (name == "圆周率" || name == "π" || name == "自然常数") {
            if (!imports_.count("数学库"))
                err(line, "「" + name + "」属于数学库，请先在文件开头写「依赖 数学库」");
            if (name == "自然常数") return "2.71828182845904523536";
            return "3.14159265358979323846";
        }
        err(line, "变量「" + name + "」没有定义");
    }

    string fresh(const string& p) { return p + std::to_string(temp_++); }

    bool isBoolExpr(const ExprPtr& e) const {
        if (e->k == Expr::K::Bool) return true;
        if (e->k == Expr::K::Unary && e->op == "非") return true;
        if (e->k == Expr::K::Binary) {
            const string& op = e->op;
            return op == "==" || op == "=" || op == "!=" || op == "<" ||
                   op == "<=" || op == ">" || op == ">=" || op == "且" ||
                   op == "或" || op == "在";
        }
        if (e->k == Expr::K::Call && e->args[0]->k == Expr::K::Var &&
            e->args[0]->text == "布尔")
            return true;
        return false;
    }

    string expr(const ExprPtr& e) {
        switch (e->k) {
            case Expr::K::Num: return cppNumber(e->num);
            case Expr::K::Bool: return e->b ? "true" : "false";
            case Expr::K::Str:
                err(e->line, "编译模式里文本只能直接放在「打印」里");
            case Expr::K::None:
                err(e->line, "编译模式暂不支持「空」");
            case Expr::K::Var:
                // 零参数内置「随机」可以省括号：编译模式里也按调用处理。
                if (e->text == "随机") {
                    if (!imports_.count("数学库"))
                        err(e->line, "「随机」属于数学库，请先在文件开头写「依赖 数学库」");
                    return "rand_real()";
                }
                return varRef(e->text, e->line);
            case Expr::K::Unary: {
                string a = expr(e->args[0]);
                if (e->op == "-") return "(-(" + a + "))";
                if (e->op == "+") return a;
                if (e->op == "非") return "(!(" + a + "))";
                err(e->line, "未知一元运算：" + e->op);
            }
            case Expr::K::Binary: {
                string a = expr(e->args[0]);
                string b = expr(e->args[1]);
                const string& op = e->op;
                if (op == "+") return "((" + a + ") + (" + b + "))";
                if (op == "-") return "((" + a + ") - (" + b + "))";
                if (op == "*") return "((" + a + ") * (" + b + "))";
                if (op == "/") return "safe_div((" + a + "), (" + b + "), " + std::to_string(e->line) + ")";
                if (op == "//") return "safe_floor_div((" + a + "), (" + b + "), " + std::to_string(e->line) + ")";
                if (op == "%") return "safe_mod((" + a + "), (" + b + "), " + std::to_string(e->line) + ")";
                if (op == "**") return "safe_pow((" + a + "), (" + b + "), " + std::to_string(e->line) + ")";
                if (op == "==" || op == "=") return "((" + a + ") == (" + b + "))";
                if (op == "!=") return "((" + a + ") != (" + b + "))";
                if (op == "<") return "((" + a + ") < (" + b + "))";
                if (op == "<=") return "((" + a + ") <= (" + b + "))";
                if (op == ">") return "((" + a + ") > (" + b + "))";
                if (op == ">=") return "((" + a + ") >= (" + b + "))";
                if (op == "且") return "((" + a + ") && (" + b + "))";
                if (op == "或") return "((" + a + ") || (" + b + "))";
                if (op == "在") err(e->line, "编译模式暂不支持「在…中」");
                err(e->line, "未知运算符：" + op);
            }
            case Expr::K::Call: {
                if (e->args[0]->k != Expr::K::Var)
                    err(e->line, "编译模式只支持按名字调用函数");
                const string& name = e->args[0]->text;
                vector<string> args;
                for (size_t i = 1; i < e->args.size(); ++i)
                    args.push_back(expr(e->args[i]));
                auto modIt = functionModules().find(name);
                if (modIt != functionModules().end() && modIt->second == "数学库" &&
                    !imports_.count("数学库")) {
                    err(e->line, "「" + name +
                                     "」属于数学库，请先在文件开头写「依赖 数学库」");
                }
                auto fit = funcs_.find(name);
                if (fit != funcs_.end()) {
                    if (args.size() != fit->second->params.size())
                        err(e->line, "函数「" + name + "」需要 " +
                                         std::to_string(fit->second->params.size()) +
                                         " 个参数，收到 " + std::to_string(args.size()) + " 个");
                    string out = fit->second->cppName + "(";
                    for (size_t i = 0; i < args.size(); ++i) {
                        if (i) out += ", ";
                        out += args[i];
                    }
                    out += ")";
                    return out;
                }
                auto need = [&](size_t n) {
                    if (args.size() != n)
                        err(e->line, "「" + name + "」需要 " + std::to_string(n) +
                                         " 个参数，收到 " + std::to_string(args.size()) + " 个");
                };
                if (name == "正弦") { need(1); return "std::sin(" + args[0] + ")"; }
                if (name == "余弦") { need(1); return "std::cos(" + args[0] + ")"; }
                if (name == "正切") { need(1); return "std::tan(" + args[0] + ")"; }
                if (name == "反正弦") { need(1); return "std::asin(" + args[0] + ")"; }
                if (name == "反余弦") { need(1); return "std::acos(" + args[0] + ")"; }
                if (name == "反正切") { need(1); return "std::atan(" + args[0] + ")"; }
                if (name == "反正切2") { need(2); return "std::atan2(" + args[0] + ", " + args[1] + ")"; }
                if (name == "双曲正弦") { need(1); return "std::sinh(" + args[0] + ")"; }
                if (name == "双曲余弦") { need(1); return "std::cosh(" + args[0] + ")"; }
                if (name == "双曲正切") { need(1); return "std::tanh(" + args[0] + ")"; }
                if (name == "绝对值") { need(1); return "std::fabs(" + args[0] + ")"; }
                if (name == "取整") { need(1); return "std::round(" + args[0] + ")"; }
                if (name == "向下取整") { need(1); return "std::floor(" + args[0] + ")"; }
                if (name == "向上取整") { need(1); return "std::ceil(" + args[0] + ")"; }
                if (name == "指数") { need(1); return "std::exp(" + args[0] + ")"; }
                if (name == "对数10") { need(1); return "safe_log10(" + args[0] + ", " + std::to_string(e->line) + ")"; }
                if (name == "对数") {
                    if (args.size() == 1)
                        return "safe_log(" + args[0] + ", " + std::to_string(e->line) + ")";
                    if (args.size() == 2)
                        return "safe_logb(" + args[0] + ", " + args[1] + ", " + std::to_string(e->line) + ")";
                    err(e->line, "「对数」需要 1 或 2 个参数");
                }
                if (name == "平方根") { need(1); return "safe_sqrt(" + args[0] + ", " + std::to_string(e->line) + ")"; }
                if (name == "幂") { need(2); return "safe_pow(" + args[0] + ", " + args[1] + ", " + std::to_string(e->line) + ")"; }
                if (name == "符号") { need(1); return "sign(" + args[0] + ")"; }
                if (name == "整数") { need(1); return "std::trunc(" + args[0] + ")"; }
                if (name == "小数") { need(1); return "(" + args[0] + ")"; }
                if (name == "布尔") { need(1); return "((" + args[0] + ") != 0.0)"; }
                if (name == "角度转弧度") {
                    need(1);
                    return "((" + args[0] + ") * 3.14159265358979323846 / 180.0)";
                }
                if (name == "弧度转角度") {
                    need(1);
                    return "((" + args[0] + ") * 180.0 / 3.14159265358979323846)";
                }
                if (name == "随机") { need(0); return "rand_real()"; }
                if (name == "随机整数") { need(2); return "rand_int(" + args[0] + ", " + args[1] + ")"; }
                if (name == "二次函数") {
                    need(4);
                    return "((" + args[0] + ") * (" + args[3] + ") * (" + args[3] + ") + (" +
                           args[1] + ") * (" + args[3] + ") + (" + args[2] + "))";
                }
                if (name == "二元二次求值") {
                    need(8);
                    return "((" + args[0] + ") * (" + args[6] + ") * (" + args[6] + ") + (" +
                           args[1] + ") * (" + args[6] + ") * (" + args[7] + ") + (" +
                           args[2] + ") * (" + args[7] + ") * (" + args[7] + ") + (" +
                           args[3] + ") * (" + args[6] + ") + (" + args[4] + ") * (" +
                           args[7] + ") + (" + args[5] + "))";
                }
                if (name == "解二次方程" || name == "解二元一次方程组" ||
                    name == "解线性方程组" || name == "多项式求值" ||
                    name == "最大" || name == "最小" || name == "求和") {
                    err(e->line, "编译模式暂不支持返回字典/列表的「" + name +
                                     "」；请用标量公式，或用解释模式运行");
                }
                if (name == "文本")
                    err(e->line, "「文本」在编译模式里只能直接放在「打印」里");
                err(e->line, "编译模式不认识的函数「" + name + "」");
            }
            case Expr::K::Index:
                err(e->line, "编译模式暂不支持下标（列表/字典）");
            case Expr::K::Slice:
                err(e->line, "编译模式暂不支持切片");
            case Expr::K::List:
                err(e->line, "编译模式暂不支持列表");
            case Expr::K::Dict:
                err(e->line, "编译模式暂不支持字典");
        }
        err(e->line, "未知表达式");
    }

    string printArg(const ExprPtr& e) {
        if (e->k == Expr::K::Str) return cppQuote(e->text);
        if (e->k == Expr::K::Call && e->args[0]->k == Expr::K::Var &&
            e->args[0]->text == "文本") {
            if (e->args.size() != 2) err(e->line, "「文本」需要 1 个参数");
            return "fmt(" + expr(e->args[1]) + ")";
        }
        string x = expr(e);
        if (isBoolExpr(e)) return "fmtb(" + x + ")";
        return "fmt(" + x + ")";
    }

    string block(const vector<StmtPtr>& body, int indent) {
        string out;
        for (const auto& s : body) out += stmt(s, indent);
        return out;
    }

    string stmt(const StmtPtr& s, int indent) {
        string ind((size_t)indent * 4, ' ');
        switch (s->k) {
            case Stmt::K::Assign: {
                if (s->target->k != Expr::K::Var)
                    err(s->line, "编译模式只支持给变量赋值");
                return ind + varRef(s->target->text, s->line) + " = " + expr(s->expr) + ";\n";
            }
            case Stmt::K::Print: {
                string out = ind + "std::cout";
                if (s->args.empty()) {
                    out += " << \"\\n\";\n";
                    return out;
                }
                for (size_t i = 0; i < s->args.size(); ++i) {
                    if (i) out += " << \" \"";
                    out += " << " + printArg(s->args[i]);
                }
                out += " << \"\\n\";\n";
                return out;
            }
            case Stmt::K::If: {
                string out = ind + "if (" + expr(s->branches[0].first) + ") {\n";
                out += block(s->branches[0].second, indent + 1);
                out += ind + "}";
                for (size_t i = 1; i < s->branches.size(); ++i) {
                    out += " else if (" + expr(s->branches[i].first) + ") {\n";
                    out += block(s->branches[i].second, indent + 1);
                    out += ind + "}";
                }
                if (!s->elseBody.empty()) {
                    out += " else {\n";
                    out += block(s->elseBody, indent + 1);
                    out += ind + "}";
                }
                out += "\n";
                return out;
            }
            case Stmt::K::While: {
                string out = ind + "while (" + expr(s->expr) + ") {\n";
                out += block(s->body, indent + 1);
                out += ind + "}\n";
                return out;
            }
            case Stmt::K::For: {
                if (s->expr->k != Expr::K::Call || s->expr->args[0]->k != Expr::K::Var ||
                    s->expr->args[0]->text != "范围") {
                    err(s->line, "编译模式的「对于」只支持 在 范围(...) 中");
                }
                size_t n = s->expr->args.size() - 1;
                if (n < 1 || n > 3)
                    err(s->line, "「范围」需要 1 到 3 个参数");
                string start = "0.0", stop, step = "1.0";
                if (n == 1) {
                    stop = expr(s->expr->args[1]);
                } else {
                    start = expr(s->expr->args[1]);
                    stop = expr(s->expr->args[2]);
                    if (n == 3) step = expr(s->expr->args[3]);
                }
                string var = varRef(s->name, s->line);
                string a = fresh("_s"), b = fresh("_e"), c = fresh("_d");
                string out = ind + "{\n";
                out += ind + "    double " + a + " = " + start + ";\n";
                out += ind + "    double " + b + " = " + stop + ";\n";
                out += ind + "    double " + c + " = " + step + ";\n";
                out += ind + "    " + var + " = " + a + ";\n";
                out += ind + "    for (; (" + c + " > 0 ? " + var + " < " + b + " : " +
                       var + " > " + b + "); " + var + " += " + c + ") {\n";
                out += block(s->body, indent + 2);
                out += ind + "    }\n";
                out += ind + "}\n";
                return out;
            }
            case Stmt::K::Func:
                return "";
            case Stmt::K::Return:
                return ind + "return " + (s->expr ? expr(s->expr) : string("0.0")) + ";\n";
            case Stmt::K::Break:
                return ind + "break;\n";
            case Stmt::K::Continue:
                return ind + "continue;\n";
            case Stmt::K::Import:
                return "";
            case Stmt::K::Export:
                err(s->line, "「导出」只能写在库里");
            case Stmt::K::Try:
                err(s->line, "编译模式暂不支持「尝试/捕获」");
            case Stmt::K::ExprStmt:
                return ind + "(void)(" + expr(s->expr) + ");\n";
        }
        return "";
    }
};

string CppGenerator::generate() {
    std::ostringstream out;
    out << cppPreamble();

    for (const auto& kv : funcs_) {
        const auto& fi = *kv.second;
        out << "double " << fi.cppName << "(";
        for (size_t i = 0; i < fi.params.size(); ++i) {
            if (i) out << ", ";
            out << "double " << fi.localCpp.at(fi.params[i]);
        }
        out << ");\n";
    }
    out << "\n";

    for (const auto& kv : globals_) out << "double " << kv.second << " = 0.0;\n";
    out << "\n";

    for (const auto& kv : funcs_) {
        const auto& fi = *kv.second;
        out << "double " << fi.cppName << "(";
        for (size_t i = 0; i < fi.params.size(); ++i) {
            if (i) out << ", ";
            out << "double " << fi.localCpp.at(fi.params[i]);
        }
        out << ") {\n";
        for (const auto& l : fi.locals) {
            out << "    double " << fi.localCpp.at(l) << " = 0.0;\n";
        }
        cur_ = kv.second.get();
        for (const auto& st : fi.stmt->body) out << stmt(st, 1);
        out << "    return 0.0;\n}\n\n";
        cur_ = nullptr;
    }

    out << "int main() {\n";
    out << "    ios::sync_with_stdio(false);\n";
    out << "    cin.tie(nullptr);\n";
    cur_ = nullptr;
    for (const auto& s : program_) {
        if (s->k != Stmt::K::Func && s->k != Stmt::K::Import) {
            out << stmt(s, 1);
        }
    }
    out << "    return 0;\n}\n";
    return out.str();
}

// ============================================================
// 12. 命令行 / REPL
// ============================================================

static string errorOrigin(const string& kind) {
    if (kind == "编译错误") return "编译模式";
    if (kind == "引擎错误") return "引擎内部";
    return "你的脚本";
}

static string errorExplanation(const string& kind) {
    if (kind == "词法错误")
        return "脚本里有无法识别的字符，或文本/引号没有闭合。";
    if (kind == "语法错误")
        return "脚本结构不符合语法，例如缺「则」/「结束」、连续空了两行、括号不配对。";
    if (kind == "运行时错误")
        return "语法没问题，但执行到这一步失败，例如变量不存在、类型不匹配、除零、下标越界。";
    if (kind == "编译错误")
        return "这段脚本含有编译模式暂不支持的写法。";
    if (kind == "文件错误")
        return "文件路径不存在、没有读写权限，或目录创建失败。";
    if (kind == "库冲突")
        return "两个库或用户代码定义了同名函数；改名，或去掉其中一个依赖。";
    if (kind == "权限错误")
        return "需要管理员权限或危险模式；先设置 WEN_ADMIN_TOKEN 并申请管理员。";
    return "解释器内部异常。";
}

static void printWenError(const WenError& e, const string& filename,
                          const vector<string>& lines) {
    if (false) std::cerr << WEN_EMBED_MARK;  // 不可达分支：只为把标记留在二进制
    std::cout.flush();
    std::cerr << "【" << e.kind << "】\n";
    std::cerr << "来源：" << errorOrigin(e.kind) << "\n";
    std::cerr << "位置：" << (filename.empty() ? "<未知>" : filename)
              << " 第 " << e.line << " 行";
    if (e.col > 0) std::cerr << " 第 " << e.col << " 列";
    std::cerr << "\n";
    std::cerr << "原因：" << e.what() << "\n";
    std::cerr << "说明：" << errorExplanation(e.kind) << "\n";
    if (e.line >= 1 && (size_t)e.line <= lines.size()) {
        std::cerr << "  " << std::setw(4) << e.line << " | "
                  << lines[(size_t)e.line - 1] << "\n";
        if (e.col > 0) {
            std::cerr << "       | "
                      << string((size_t)std::max(0, e.col - 1), ' ') << "^\n";
        }
    }
    if (!e.frames.empty()) {
        std::cerr << "调用栈：\n";
        for (auto it = e.frames.rbegin(); it != e.frames.rend(); ++it)
            std::cerr << "  " << *it << "\n";
    }
}

static void printIncompleteError(const IncompleteError& e, const string& filename,
                                 const vector<string>& lines) {
    WenError we("语法错误", e.line, 0, e.what());
    printWenError(we, filename, lines);
}

static bool validUtf8(const string& s) {
    if (s.empty()) return true;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(),
                               nullptr, 0) > 0;
}

static uint32_t rdU32(const string& b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return (uint32_t)(unsigned char)b[o] | ((uint32_t)(unsigned char)b[o + 1] << 8) |
           ((uint32_t)(unsigned char)b[o + 2] << 16) |
           ((uint32_t)(unsigned char)b[o + 3] << 24);
}
static uint16_t rdU16(const string& b, size_t o) {
    if (o + 2 > b.size()) return 0;
    return (uint16_t)((uint16_t)(unsigned char)b[o] |
                      ((uint16_t)(unsigned char)b[o + 1] << 8));
}

static Value binaryHeader(const string& path, int line) {
    string b;
    try {
        b = readWholeFile(path);
    } catch (IoError& e) {
        throw WenError("文件错误", line, 0, e.what());
    }
    auto d = std::make_shared<Dict>();
    d->items["大小"] = vNum((double)b.size());
    auto sections = std::make_shared<List>();
    string format = "未知";
    int bits = 0;
    unsigned long long entry = 0;
    if (b.rfind("MZ", 0) == 0 && b.size() > 0x40) {
        format = "PE";
        uint32_t pe = rdU32(b, 0x3c);
        if (pe + 24 <= b.size() && b.compare(pe, 4, string("PE\0\0", 4)) == 0) {
            uint16_t nsec = rdU16(b, pe + 6);
            uint16_t optSize = rdU16(b, pe + 20);
            size_t opt = pe + 24;
            uint16_t magic = rdU16(b, opt);
            bits = magic == 0x20b ? 64 : 32;
            entry = rdU32(b, opt + 16);
            size_t sec = opt + optSize;
            for (uint16_t i = 0; i < nsec && sec + 40 <= b.size(); ++i, sec += 40) {
                string name = b.substr(sec, 8);
                name = name.c_str();
                auto s = std::make_shared<Dict>();
                s->items["名字"] = vStr(name);
                s->items["地址"] = vNum((double)rdU32(b, sec + 12));
                s->items["大小"] = vNum((double)rdU32(b, sec + 8));
                sections->items.push_back(vDict(s));
            }
        }
    } else if (b.size() > 20 && (unsigned char)b[0] == 0x7f && b.compare(1, 3, "ELF") == 0) {
        format = "ELF";
        bits = (unsigned char)b[4] == 2 ? 64 : 32;
        entry = bits == 64 ? (unsigned long long)rdU32(b, 24) | ((unsigned long long)rdU32(b, 28) << 32)
                           : rdU32(b, 24);
        size_t shoff = bits == 64 ? (size_t)rdU32(b, 40) | ((size_t)rdU32(b, 44) << 32)
                                  : rdU32(b, 32);
        uint16_t shentsize = rdU16(b, bits == 64 ? 58 : 46);
        uint16_t shnum = rdU16(b, bits == 64 ? 60 : 48);
        uint16_t shstr = rdU16(b, bits == 64 ? 62 : 50);
        string names;
        if (shstr < shnum) {
            size_t so = shoff + (size_t)shstr * shentsize;
            size_t noff = bits == 64 ? rdU32(b, so + 24) : rdU32(b, so + 16);
            size_t nsize = bits == 64 ? rdU32(b, so + 32) : rdU32(b, so + 20);
            if (noff + nsize <= b.size()) names = b.substr(noff, nsize);
        }
        for (uint16_t i = 0; i < shnum && i < 64; ++i) {
            size_t so = shoff + (size_t)i * shentsize;
            if (so + shentsize > b.size()) break;
            uint32_t nidx = rdU32(b, so);
            string name;
            if (nidx < names.size()) name = names.c_str() + nidx;
            auto s = std::make_shared<Dict>();
            s->items["名字"] = vStr(name);
            s->items["地址"] = vNum((double)(bits == 64 ? rdU32(b, so + 16) : rdU32(b, so + 12)));
            s->items["大小"] = vNum((double)(bits == 64 ? rdU32(b, so + 32) : rdU32(b, so + 20)));
            sections->items.push_back(vDict(s));
        }
    } else if (b.rfind("PK", 0) == 0) {
        format = "ZIP";
    } else if (b.rfind("\x89" "PNG", 0) == 0) {
        format = "PNG";
    }
    d->items["格式"] = vStr(format);
    d->items["位宽"] = vNum((double)bits);
    d->items["入口"] = vNum((double)entry);
    d->items["节"] = vList(sections);
    return vDict(d);
}

static vector<string> binaryStrings(const string& bytes, size_t minLen, size_t limit,
                                    const string& filter, int line) {
    vector<string> out;
    std::set<string> seen;
    std::regex re;
    bool useRe = false;
    if (filter.size() > 2 && filter.front() == '/' && filter.back() == '/') {
        try {
            re = std::regex(filter.substr(1, filter.size() - 2));
            useRe = true;
        } catch (std::regex_error& e) {
            throw WenError("运行时错误", line, 0, string("过滤正则错误：") + e.what());
        }
    }
    auto pass = [&](const string& s) {
        if (s.size() < minLen) return false;
        if (useRe) return std::regex_search(s, re);
        if (!filter.empty()) return s.find(filter) != string::npos;
        return true;
    };
    auto flush = [&](string& cur) {
        if (!cur.empty() && validUtf8(cur) && pass(cur) && seen.insert(cur).second)
            out.push_back(cur);
        cur.clear();
    };
    string cur;
    for (unsigned char c : bytes) {
        if (c < 32 || c == 127) flush(cur);
        else cur += (char)c;
        if (out.size() >= limit) return out;
    }
    flush(cur);
    for (size_t i = 0; i + 1 < bytes.size() && out.size() < limit;) {
        string u;
        while (i + 1 < bytes.size() && (unsigned char)bytes[i + 1] == 0 &&
               (unsigned char)bytes[i] >= 32 && (unsigned char)bytes[i] != 127) {
            u += bytes[i];
            i += 2;
        }
        if (!u.empty() && pass(u) && seen.insert(u).second) out.push_back(u);
        i += (u.empty() ? 1 : 0);
    }
    return out;
}

static Value binaryStringsValue(const string& path, int line, size_t minLen, size_t limit,
                                const string& filter) {
    string b;
    try {
        b = readWholeFile(path);
    } catch (IoError& e) {
        throw WenError("文件错误", line, 0, e.what());
    }
    auto l = std::make_shared<List>();
    for (const auto& s : binaryStrings(b, minLen, limit, filter, line))
        l->items.push_back(vStr(s));
    return vList(l);
}
static vector<string> disassembleBytecode(const string& path, int line) {
    string text;
    try {
        text = readWholeFile(path);
    } catch (IoError& e) {
        throw WenError("文件错误", line, 0, e.what());
    }
    JsonReader reader(text, line);
    auto root = needDict(reader.parse(), "字节码", line);
    auto code = needList(root->items.at("code"), "字节码", line);
    vector<string> out;
    for (size_t i = 0; i < code->items.size(); ++i) {
        auto ins = needDict(code->items[i], "字节码指令", line);
        string op = needStr(ins->items.at("op"), "字节码 op", line);
        std::ostringstream s;
        s << std::setw(4) << std::setfill('0') << i << "  " << op;
        if (ins->items.count("v")) s << " " << toText(ins->items["v"], true);
        if (ins->items.count("name")) s << " " << needStr(ins->items["name"], "name", line);
        if (ins->items.count("n")) s << " " << needNum(ins->items["n"], "n", line);
        if (ins->items.count("to")) s << " -> " << needNum(ins->items["to"], "to", line);
        out.push_back(s.str());
    }
    return out;
}
static string translateToCppSource(const vector<StmtPtr>& program, vector<string>& diags) {
    try {
        CppGenerator gen(program);
        return gen.generate();
    } catch (CompileError& e) {
        diags.push_back("第 " + std::to_string(e.line) + " 行：" + e.what());
    } catch (WenError& e) {
        diags.push_back("第 " + std::to_string(e.line) + " 行：" + e.what());
    }
    return "";
}

static void printCompileError(const CompileError& e, const string& filename) {
    std::cout.flush();
    std::cerr << "【编译错误】\n";
    std::cerr << "来源：编译模式\n";
    std::cerr << "位置：" << (filename.empty() ? "<未知>" : filename)
              << " 第 " << e.line << " 行\n";
    std::cerr << "原因：" << e.what() << "\n";
    std::cerr << "说明：" << errorExplanation("编译错误") << "\n";
}

static void printIoError(const IoError& e, const string& context) {
    std::cout.flush();
    std::cerr << "【文件错误】\n";
    std::cerr << "来源：你的脚本（文件路径或权限）\n";
    if (!context.empty()) std::cerr << "位置：" << context << "\n";
    std::cerr << "原因：" << e.what() << "\n";
    std::cerr << "说明：" << errorExplanation("文件错误") << "\n";
}

static void printEngineError(const std::exception& e, const string& context) {
    std::cout.flush();
    std::cerr << "【引擎错误】\n";
    std::cerr << "来源：引擎内部（不是你的脚本写错）\n";
    if (!context.empty()) std::cerr << "位置：" << context << "\n";
    std::cerr << "详情：" << e.what() << "\n";
    std::cerr << "说明：这是解释器自身的异常；请把这段脚本和这条错误一起反馈。\n";
}

static void repl(Interpreter& vm) {
    std::cout << "=== 「文」语言 C++ 版 v0.1 ===\n";
    std::cout << "输入「退出」离开；多行块会自动续行。\n\n";
    string buffer;
    while (true) {
        std::cout << (buffer.empty() ? "文>>> " : " ... ") << std::flush;
        string line;
        if (!std::getline(std::cin, line)) break;
        stripBom(line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (buffer.empty()) {
            string t = trim(line);
            if (t == "退出" || t == "再见") break;
            if (t.empty()) continue;
        }
        buffer += line;
        buffer += "\n";
        vm.filename = "<交互>";
        vm.sourceLines = splitLines(buffer);
        try {
            auto program = vm.parseSource(buffer);
            vm.execProgram(program);
            if (vm.lastWasExpr && vm.lastValue.type != VT::None) {
                std::cout << "=> " << toText(vm.lastValue, true) << "\n";
            }
            buffer.clear();
        } catch (IncompleteError&) {
            // 块还没写完，继续读下一行
        } catch (WenError& e) {
            printWenError(e, "<交互>", splitLines(buffer));
            buffer.clear();
        } catch (std::exception& e) {
            printEngineError(e, "<交互>");
            buffer.clear();
        }
    }
}

static string stripPathQuotes(string s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    if (s.size() >= 6) {
        if ((startsWith(s, 0, "“") && s.compare(s.size() - 3, 3, "”") == 0) ||
            (startsWith(s, 0, "「") && s.compare(s.size() - 3, 3, "」") == 0)) {
            return s.substr(3, s.size() - 6);
        }
    }
    return s;
}

// 处理源码里的「存入 路径 … 存入」块：把中间原文保存到文件。
// 行数保持不变（块内每行变成注释），所以后面的报错行号不会乱。
static int processSaveBlocks(const string& src, bool write, string& out,
                             vector<WenError>& errors) {
    const string kw = "存入";
    vector<string> lines = splitLines(src);
    vector<string> outLines;
    int blocks = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        string t = trim(lines[i]);
        if (t.size() > kw.size() && t.compare(0, kw.size(), kw) == 0) {
            string rest = trim(t.substr(kw.size()));
            if (rest.empty()) {
                errors.push_back(WenError("语法错误", (int)i + 1, 0,
                                          "「存入」后面要写文件路径"));
                outLines.push_back("# " + lines[i]);
                continue;
            }
            string path = stripPathQuotes(rest);
            size_t j = i + 1;
            while (j < lines.size() && trim(lines[j]) != kw) ++j;
            if (j >= lines.size()) {
                errors.push_back(WenError(
                    "语法错误", (int)i + 1, 0,
                    "「存入」没有结束；最后要再用一行「存入」收尾"));
                for (size_t k = i; k < lines.size(); ++k)
                    outLines.push_back("# " + lines[k]);
                break;
            }
            string content;
            for (size_t k = i + 1; k < j; ++k) {
                content += lines[k];
                if (k + 1 < j) content += "\n";
            }
            if (j > i + 1) content += "\n";
            if (write) {
                try {
                    ensureParentDirectory(path);
                    writeWholeFile(path, content, false);
                } catch (IoError& e) {
                    errors.push_back(WenError("文件错误", (int)i + 1, 0, e.what()));
                }
            }
            ++blocks;
            for (size_t k = i; k <= j; ++k) outLines.push_back("# " + lines[k]);
            i = j;
            continue;
        }
        outLines.push_back(lines[i]);
    }
    out.clear();
    for (size_t i = 0; i < outLines.size(); ++i) {
        if (i) out += "\n";
        out += outLines[i];
    }
    if (!out.empty()) out += "\n";
    return blocks;
}

static vector<StmtPtr> parseDiagnostic(const string& src, vector<WenError>& errors) {
    try {
        Lexer lex(src);
        Parser parser(lex.scan(), true);
        auto ast = parser.parseProgram();
        errors = parser.errors();
        return ast;
    } catch (WenError& e) {
        errors.push_back(e);
        return {};
    }
}

static bool gJsonDiagnostics = false;  // --json：诊断以 JSON 输出到 stdout

static void printDiagnostics(const vector<WenError>& errors, const string& filename,
                             const vector<string>& lines) {
    if (gJsonDiagnostics) {
        string out = "{\"ok\":false,\"file\":" + jsonQuote(filename) +
                     ",\"count\":" + std::to_string(errors.size()) + ",\"diagnostics\":[";
        for (size_t i = 0; i < errors.size(); ++i) {
            const WenError& e = errors[i];
            if (i) out += ",";
            out += "{\"kind\":" + jsonQuote(e.kind) +
                   ",\"line\":" + std::to_string(e.line) +
                   ",\"column\":" + std::to_string(e.col) +
                   ",\"reason\":" + jsonQuote(e.what()) +
                   ",\"detail\":" + jsonQuote(errorExplanation(e.kind));
            if (e.line >= 1 && (size_t)e.line <= lines.size()) {
                out += ",\"sourceLine\":{\"number\":" + std::to_string(e.line) +
                       ",\"text\":" + jsonQuote(lines[(size_t)e.line - 1]) + "}";
            }
            out += "}";
        }
        out += "]}";
        std::cout << out << "\n";
        std::cout.flush();
        return;
    }
    std::cout.flush();
    std::cerr << "发现 " << errors.size() << " 个错误：\n\n";
    for (size_t i = 0; i < errors.size(); ++i) {
        std::cerr << "错误 " << (i + 1) << "/" << errors.size() << "\n";
        printWenError(errors[i], filename, lines);
        std::cerr << "\n";
    }
    std::cerr << "共 " << errors.size() << " 个错误。\n";
}

static int runFile(const string& path) {
    Interpreter vm;
    try {
        string src = readWholeFile(resolveWenPath(path));
        vector<WenError> errors;
        string expanded;
        processSaveBlocks(src, false, expanded, errors);
        if (!errors.empty()) {
            printDiagnostics(errors, path, splitLines(src));
            return 1;
        }
        auto ast = parseDiagnostic(expanded, errors);
        if (!errors.empty()) {
            printDiagnostics(errors, path, splitLines(expanded));
            return 1;
        }
        string written;
        vector<WenError> writeErrors;
        processSaveBlocks(src, true, written, writeErrors);
        if (!writeErrors.empty()) {
            printDiagnostics(writeErrors, path, splitLines(src));
            return 1;
        }
        vector<string> lines = splitLines(expanded);
        vm.filename = path;
        vm.sourceLines = lines;
        vm.scanPull(expanded);
        vm.execProgram(ast);
        return 0;
    } catch (WenError& e) {
        printWenError(e, path, vm.sourceLines);
        return 1;
    } catch (IncompleteError& e) {
        printIncompleteError(e, path, vm.sourceLines);
        return 1;
    } catch (IoError& e) {
        printIoError(e, path);
        return 1;
    } catch (std::exception& e) {
        printEngineError(e, path);
        return 1;
    }
}

static int checkFile(const string& path) {
    vector<string> lines;
    try {
        string src = readWholeFile(resolveWenPath(path));
        vector<WenError> errors;
        string expanded;
        processSaveBlocks(src, false, expanded, errors);
        if (!errors.empty()) {
            lines = splitLines(src);
            printDiagnostics(errors, path, lines);
            return 1;
        }
        auto program = parseDiagnostic(expanded, errors);
        lines = splitLines(expanded);
        if (!errors.empty()) {
            printDiagnostics(errors, path, lines);
            return 1;
        }
        if (gJsonDiagnostics) {
            std::cout << "{\"ok\":true,\"file\":" << jsonQuote(path)
                      << ",\"count\":0,\"statementCount\":" << program.size()
                      << ",\"diagnostics\":[]}\n";
            return 0;
        }
        std::cout << "语法通过：顶层 " << program.size() << " 条语句\n";
        return 0;
    } catch (IncompleteError& e) {
        printIncompleteError(e, path, lines);
        return 1;
    } catch (WenError& e) {
        printWenError(e, path, lines);
        return 1;
    } catch (IoError& e) {
        printIoError(e, path);
        return 1;
    } catch (std::exception& e) {
        printEngineError(e, path);
        return 1;
    }
}

static string findCompiler() {
    static const vector<string> candidates = {
        "C:\\msys64\\ucrt64\\bin\\g++.exe",
        "C:\\Users\\Acer\\Desktop\\w64devkit\\bin\\g++.exe",
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(fs::u8path(p), ec) && !ec) return p;
    }
    return "g++";
}

static int runCommand(const string& cmd) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
    if (n <= 0) return -1;
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, w.data(), n);
    return _wsystem(w.c_str());
#else
    return std::system(cmd.c_str());
#endif
}

static int dumpTokens(const string& path) {
    vector<string> lines;
    try {
        string src = readWholeFile(resolveWenPath(path));
        lines = splitLines(src);
        Lexer lex(src);
        auto toks = lex.scan();
        for (const auto& t : toks) {
            std::cout << (int)t.type << " |" << t.text << "| 行 " << t.line
                      << " 列 " << t.col << "\n";
        }
        return 0;
    } catch (WenError& e) {
        printWenError(e, path, lines);
        return 1;
    } catch (IoError& e) {
        printIoError(e, path);
        return 1;
    } catch (std::exception& e) {
        printEngineError(e, path);
        return 1;
    }
}

static int createLibrary(const string& name, const string& output) {
    if (name.empty()) {
        std::cerr << "库名不能为空\n";
        return 1;
    }
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == ' ') {
            std::cerr << "库名里不能有路径/特殊字符：" << name << "\n";
            return 1;
        }
    }
    if (std::isdigit((unsigned char)name[0])) {
        std::cerr << "库名不能以数字开头\n";
        return 1;
    }

    std::error_code ec;
    fs::path path = output.empty()
                        ? (fs::current_path(ec) / fs::u8path("库") /
                           fs::u8path(name + ".wen"))
                        : fs::u8path(output);
    if (fs::exists(path, ec) && !ec) {
        std::cerr << "已经存在：" << path.u8string() << "\n";
        return 1;
    }
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        printIoError(IoError("无法创建目录：" + ec.message()),
                     path.parent_path().u8string());
        return 1;
    }
    string code =
        "# " + name + ".wen —— 自定义库模板\n" +
        "# 用法：在脚本开头写  依赖 " + name + "\n" +
        "函数 " + name + "示例 x 则\n" +
        "    返回 x\n" +
        "结束\n" +
        "导出 " + name + "示例\n";
    try {
        writeWholeFile(path.u8string(), code, false);
    } catch (IoError& e) {
        printIoError(e, path.u8string());
        return 1;
    } catch (std::exception& e) {
        printEngineError(e, path.u8string());
        return 1;
    }
    std::cout << "已生成库模板：" << path.u8string() << "\n";
    return 0;
}

static int compileFile(const string& path, const string& output, bool build) {
    vector<string> lines;
    try {
        string src = readWholeFile(resolveWenPath(path));
        vector<WenError> errors;
        string expanded;
        int blocks = processSaveBlocks(src, false, expanded, errors);
        if (!errors.empty()) {
            lines = splitLines(src);
            printDiagnostics(errors, path, lines);
            return 1;
        }
        if (blocks > 0) {
            printCompileError(
                CompileError(1, "编译模式不支持「存入」；请用解释模式运行这个脚本"),
                path);
            return 1;
        }
        auto program = parseDiagnostic(expanded, errors);
        lines = splitLines(expanded);
        if (!errors.empty()) {
            printDiagnostics(errors, path, lines);
            return 1;
        }
        CppGenerator gen(program);
        string code = gen.generate();

        fs::path srcPath = fs::u8path(path);
        fs::path basePath = srcPath.parent_path() / fs::u8path(srcPath.stem().u8string());
        string base = basePath.u8string();

        if (!build) {
            string cppPath = output.empty() ? base + ".cpp" : output;
            writeWholeFile(cppPath, code, false);
            std::cout << "已生成 C++ 源码：" << cppPath << "\n";
            return 0;
        }

        // --编译 默认只留下 exe；要看 C++ 中间代码请用 --生成C++
        string exePath = output.empty() ? base + ".exe" : output;

        // g++/ld 在 Windows 上对中文输出路径不稳定：编译放在纯 ASCII 临时目录，
        // 成功后再用 std::filesystem（UTF-8）把 exe 复制到中文路径。
        std::error_code ec;
        fs::path tmpRoot = fs::temp_directory_path(ec);
        if (ec) tmpRoot = fs::current_path(ec);
        fs::path tmpDir = tmpRoot /
            ("wen_build_" + std::to_string(
                 (unsigned long long)std::chrono::steady_clock::now()
                     .time_since_epoch().count()));
        fs::create_directories(tmpDir, ec);
        if (ec) {
            std::cerr << "无法创建临时编译目录：" << ec.message() << "\n";
            return 1;
        }
        string tmpCpp = (tmpDir / "prog.cpp").u8string();
        string tmpExe = (tmpDir / "prog.exe").u8string();
        writeWholeFile(tmpCpp, code, false);

        string gxx = findCompiler();
        string dir = fs::path(gxx).parent_path().u8string();
        string cmd;
        if (!dir.empty()) cmd += "set \"PATH=" + dir + ";%PATH%\" && ";
        cmd += "\"" + gxx + "\" -std=c++17 -O2 -static -o \"" + tmpExe +
               "\" \"" + tmpCpp + "\"";
        int rc = runCommand(cmd);
        if (rc != 0) {
            fs::remove_all(tmpDir, ec);
            std::cerr << "C++ 编译失败（编译器返回 " << rc << "）\n";
            return 1;
        }
        fs::copy_file(fs::u8path(tmpExe), fs::u8path(exePath),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "复制 exe 失败：" << ec.message() << "\n";
            std::error_code ec2;
            fs::remove_all(tmpDir, ec2);
            return 1;
        }
        std::error_code ec2;
        fs::remove_all(tmpDir, ec2);
        std::cout << "已生成 " << exePath << "\n";
        return 0;
    } catch (CompileError& e) {
        printCompileError(e, path);
        return 1;
    } catch (IncompleteError& e) {
        printIncompleteError(e, path, lines);
        return 1;
    } catch (WenError& e) {
        printWenError(e, path, lines);
        return 1;
    } catch (IoError& e) {
        printIoError(e, path);
        return 1;
    } catch (std::exception& e) {
        printEngineError(e, path);
        return 1;
    }
}

static int runMain(const vector<string>& args) {
    if (args.size() <= 1) {
        Interpreter vm;
        repl(vm);
        return 0;
    }
    const string& cmd = args[1];
    for (const auto& a : args) {
        if (a == "--json" || a == "--诊断JSON") gJsonDiagnostics = true;
    }
    if (cmd == "--版本" || cmd == "-v") {
        std::cout << "文语言 v0.1 · 作者 Akm / 筑宸科技 · 2026-10-25 首次发布\n";
        return 0;
    }
    if (cmd == "--后缀") {
        for (const auto& s : WEN_SUFFIXES) std::cout << s.use << "  " << s.ext << "\n";
        return 0;
    }
    if (cmd == "--反字节码") {
        if (args.size() < 3) {
            std::cerr << "用法：wen.exe --反字节码 字节码.json [输出.txt]\n";
            return 1;
        }
        vector<string> lines;
        try {
            lines = disassembleBytecode(args[2], 1);
        } catch (WenError& e) {
            std::cerr << "【" << e.kind << "】" << e.what() << "\n";
            return 1;
        }
        string text;
        for (const auto& s : lines) text += s + "\n";
        if (args.size() >= 4) {
            try {
                ensureParentDirectory(args[3]);
                writeWholeFile(args[3], text, false);
            } catch (IoError& e) {
                std::cerr << e.what() << "\n";
                return 1;
            }
        } else {
            std::cout << text;
        }
        return 0;
    }
    if (cmd == "--二进制反汇编" || cmd == "--反二进制") {
        if (args.size() < 3) {
            std::cerr << "用法：wen.exe --二进制反汇编 文件.exe [输出.txt]\n";
            return 1;
        }
        try {
            auto d = needDict(binaryHeader(args[2], 1), "二进制反汇编", 1);
            auto strs = binaryStringsValue(args[2], 1, 8, 200, "");
            std::ostringstream out;
            out << "格式: " << toText(d->items["格式"]) << "\n";
            out << "位宽: " << toText(d->items["位宽"]) << "\n";
            out << "入口: " << toText(d->items["入口"]) << "\n";
            auto secs = std::dynamic_pointer_cast<List>(d->items["节"].list);
            out << "节数: " << (secs ? secs->items.size() : 0) << "\n";
            auto list = std::dynamic_pointer_cast<List>(strs.list);
            for (const auto& v : list->items) out << toText(v, true) << "\n";
            if (args.size() >= 4) {
                ensureParentDirectory(args[3]);
                writeWholeFile(args[3], out.str(), false);
            } else {
                std::cout << out.str();
            }
        } catch (WenError& e) {
            std::cerr << "【" << e.kind << "】" << e.what() << "\n";
            return 1;
        } catch (IoError& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
        return 0;
    }    if (cmd == "--签名") {
        std::cout << WEN_SIGNATURE << "\n";
        return 0;
    }
    if (cmd == "--库" || cmd == "--库列表") {
        Interpreter vm;
        for (const auto& lib : vm.libraryFunctions) {
            std::cout << lib.first << "（" << lib.second.size() << " 个函数）\n";
            bool first = true;
            for (const auto& fn : lib.second) {
                if (!first) std::cout << "、";
                first = false;
                std::cout << fn.first;
            }
            std::cout << "\n\n";
        }
        return 0;
    }
    if (cmd == "--检查") {
        string target;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i].rfind("--", 0) != 0) {
                target = args[i];
                break;
            }
        }
        if (target.empty()) {
            std::cerr << "用法：wen.exe --检查 [--json] 文件.wen\n";
            return 1;
        }
        return checkFile(target);
    }
    if (cmd == "--生成库") {
        if (args.size() < 3) {
            std::cerr << "用法：wen.exe --生成库 库名 [输出.wen]\n";
            return 1;
        }
        return createLibrary(args[2], args.size() >= 4 ? args[3] : "");
    }
    if (cmd == "--词法") {
        if (args.size() < 3) {
            std::cerr << "用法：wen.exe --词法 文件.wen\n";
            return 1;
        }
        return dumpTokens(args[2]);
    }
    if (cmd == "--编译" || cmd == "-c") {
        if (args.size() < 3) {
            std::cerr << "用法：wen.exe --编译 文件.wen [输出.exe]\n";
            return 1;
        }
        return compileFile(args[2], args.size() >= 4 ? args[3] : "", true);
    }
    if (cmd == "--生成C++") {
        if (args.size() < 3) {
            std::cerr << "用法：wen.exe --生成C++ 文件.wen [输出.cpp]\n";
            return 1;
        }
        return compileFile(args[2], args.size() >= 4 ? args[3] : "", false);
    }
    if (!cmd.empty() && cmd[0] == '-') {
        std::cerr << "未知选项：" << cmd << "\n";
        return 1;
    }
    return runFile(cmd);
}

#ifdef _WIN32
static string wideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize((size_t)n - 1);
    return s;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    vector<string> args;
    for (int i = 0; i < argc; ++i) args.push_back(wideToUtf8(argv[i]));
    return runMain(args);
}
#else
int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    vector<string> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    return runMain(args);
}
#endif
