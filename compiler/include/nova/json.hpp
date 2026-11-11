// Minimal ordered JSON value + serializer (no external dependencies).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace nova {

class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(int v) : type_(Type::Int), i_(v) {}
    Json(long v) : type_(Type::Int), i_(v) {}
    Json(long long v) : type_(Type::Int), i_(v) {}
    Json(unsigned v) : type_(Type::Int), i_(v) {}
    Json(unsigned long v) : type_(Type::Int), i_(static_cast<int64_t>(v)) {}
    Json(double v) : type_(Type::Double), d_(v) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Json& set(const std::string& key, Json v) {
        if (type_ != Type::Object) { *this = object(); }
        for (auto& kv : obj_) if (kv.first == key) { kv.second = std::move(v); return *this; }
        obj_.emplace_back(key, std::move(v));
        return *this;
    }
    Json& push(Json v) {
        if (type_ != Type::Array) { *this = array(); }
        arr_.push_back(std::move(v));
        return *this;
    }
    size_t size() const { return type_ == Type::Array ? arr_.size() : obj_.size(); }
    Type type() const { return type_; }

    std::string dump() const { std::string out; write(out); return out; }

    void write(std::string& out) const {
        switch (type_) {
            case Type::Null: out += "null"; break;
            case Type::Bool: out += b_ ? "true" : "false"; break;
            case Type::Int: out += std::to_string(i_); break;
            case Type::Double: {
                if (!std::isfinite(d_)) { out += "null"; break; }
                char buf[40]; std::snprintf(buf, sizeof buf, "%.17g", d_); out += buf; break;
            }
            case Type::String: writeString(out, s_); break;
            case Type::Array: {
                out += '[';
                for (size_t k = 0; k < arr_.size(); ++k) { if (k) out += ','; arr_[k].write(out); }
                out += ']'; break;
            }
            case Type::Object: {
                out += '{';
                for (size_t k = 0; k < obj_.size(); ++k) {
                    if (k) out += ',';
                    writeString(out, obj_[k].first); out += ':'; obj_[k].second.write(out);
                }
                out += '}'; break;
            }
        }
    }

    static void writeString(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                    else out += static_cast<char>(c);
            }
        }
        out += '"';
    }

private:
    Type type_ = Type::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

} // namespace nova
