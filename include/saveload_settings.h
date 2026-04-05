// saveload_settings_nostd.h
#pragma once
#include <Arduino.h>
#include "saveloadlib.h" // SaveableSettingBase, ISaveableSettingHost, sl_fnv1a_16, etc.
#include "functional-vlpp.h"

// ---------------------------------------------------------------------------
// Minimal type traits (no std:: / <type_traits> required)
// ---------------------------------------------------------------------------
template<typename A, typename B> struct sl_is_same          { static constexpr bool value = false; };
template<typename A>             struct sl_is_same<A, A>    { static constexpr bool value = true;  };

template<typename T> struct sl_is_integral                  { static constexpr bool value = false; };
template<> struct sl_is_integral<bool>                      { static constexpr bool value = true; };
template<> struct sl_is_integral<char>                      { static constexpr bool value = true; };
template<> struct sl_is_integral<signed char>               { static constexpr bool value = true; };
template<> struct sl_is_integral<unsigned char>             { static constexpr bool value = true; };
template<> struct sl_is_integral<short>                     { static constexpr bool value = true; };
template<> struct sl_is_integral<unsigned short>            { static constexpr bool value = true; };
template<> struct sl_is_integral<int>                       { static constexpr bool value = true; };
template<> struct sl_is_integral<unsigned int>              { static constexpr bool value = true; };
template<> struct sl_is_integral<long>                      { static constexpr bool value = true; };
template<> struct sl_is_integral<unsigned long>             { static constexpr bool value = true; };
template<> struct sl_is_integral<long long>                 { static constexpr bool value = true; };
template<> struct sl_is_integral<unsigned long long>        { static constexpr bool value = true; };

template<typename T> struct sl_is_floating_point            { static constexpr bool value = false; };
template<> struct sl_is_floating_point<float>               { static constexpr bool value = true; };
template<> struct sl_is_floating_point<double>              { static constexpr bool value = true; };
template<> struct sl_is_floating_point<long double>         { static constexpr bool value = true; };
// ---------------------------------------------------------------------------

#if __cplusplus >= 201703L
// ---------------------------------------------------------------------------
// C++17 path: use if constexpr for compact, readable dispatch
// ---------------------------------------------------------------------------

template<typename T>
static inline T sl_parse_from_cstr(const char* s) {
    if constexpr (sl_is_same<T, bool>::value)        { return (atoi(s) != 0); }
    else if constexpr (sl_is_integral<T>::value)     { return (T)atoll(s); }
    else if constexpr (sl_is_floating_point<T>::value){ return (T)atof(s); }
    else if constexpr (__is_enum(T))                 { return (T)(int)atoll(s); }
    else if constexpr (sl_is_same<T, const char*>::value) { return s; }
    else { static_assert(!sl_is_same<T,T>::value, "No parser for this type"); }
}

template<typename T>
static inline void sl_format_to_buf(char* buf, size_t sz, const char* lbl, T v) {
    if constexpr (sl_is_same<T, bool>::value)             { snprintf(buf, sz, "%s=%d",   lbl, v ? 1 : 0); }
    else if constexpr (sl_is_integral<T>::value)          { snprintf(buf, sz, "%s=%lld", lbl, (long long)v); }
    else if constexpr (sl_is_floating_point<T>::value)    { snprintf(buf, sz, "%s=%.6g", lbl, (double)v); }
    else if constexpr (__is_enum(T))                      { snprintf(buf, sz, "%s=%lld", lbl, (long long)v); }
    else if constexpr (sl_is_same<T, const char*>::value) { snprintf(buf, sz, "%s=%s",   lbl, v ? v : ""); }
    else                                                  { snprintf(buf, sz, "%s=0",    lbl); }
}

#else
// ---------------------------------------------------------------------------
// C++14 path: tag-dispatch structs (no if constexpr required)
// ---------------------------------------------------------------------------

// Tag: 0=bool, 1=integral(non-bool), 2=float, 3=enum/other, 4=const char*
template<typename T>
struct sl_type_tag {
    static const int value =
        sl_is_same<T, bool>::value        ? 0 :
        sl_is_integral<T>::value          ? 1 :
        sl_is_floating_point<T>::value    ? 2 :
        sl_is_same<T, const char*>::value ? 4 :
        3; // enum or unrecognised
};

template<typename T, int Tag = sl_type_tag<T>::value> struct sl_parser;
template<typename T> struct sl_parser<T, 0> { static T run(const char* s) { return (T)(atoi(s) != 0); } };
template<typename T> struct sl_parser<T, 1> { static T run(const char* s) { return (T)atoll(s); } };
template<typename T> struct sl_parser<T, 2> { static T run(const char* s) { return (T)atof(s); } };
template<typename T> struct sl_parser<T, 3> { static T run(const char* s) { return (T)(int)atoll(s); } };
template<typename T> struct sl_parser<T, 4> { static T run(const char* s) { return s; } };

template<typename T, int Tag = sl_type_tag<T>::value> struct sl_formatter;
template<typename T> struct sl_formatter<T, 0> {
    static void run(char* buf, size_t sz, const char* lbl, T v) { snprintf(buf, sz, "%s=%d",   lbl, v ? 1 : 0); } };
template<typename T> struct sl_formatter<T, 1> {
    static void run(char* buf, size_t sz, const char* lbl, T v) { snprintf(buf, sz, "%s=%lld", lbl, (long long)v); } };
template<typename T> struct sl_formatter<T, 2> {
    static void run(char* buf, size_t sz, const char* lbl, T v) { snprintf(buf, sz, "%s=%.6g", lbl, (double)v); } };
template<typename T> struct sl_formatter<T, 3> {
    static void run(char* buf, size_t sz, const char* lbl, T v) { snprintf(buf, sz, "%s=%lld", lbl, (long long)v); } };
template<typename T> struct sl_formatter<T, 4> {
    static void run(char* buf, size_t sz, const char* lbl, T v) { snprintf(buf, sz, "%s=%s",   lbl, v ? v : ""); } };

template<typename T>
static inline T sl_parse_from_cstr(const char* s) { return sl_parser<T>::run(s); }

template<typename T>
static inline void sl_format_to_buf(char* buf, size_t sz, const char* lbl, T v) {
    sl_formatter<T>::run(buf, sz, lbl, v);
}

#endif // __cplusplus >= 201703L


// callable validity helper (works for vl::Func and raw pointers)
template<typename C>
static inline bool sl_callable_valid(const C& c) {
  // vl::Func has operator bool; raw function pointers compare to nullptr
  return (bool)c;
}

// -------------------- SaveableSetting (member-function based) --------------------
template<class TargetClass, class DataType>
class SaveableSetting : public SaveableSettingBase {
public:
  TargetClass* target = nullptr;
  DataType* variable = nullptr;

  void (TargetClass::*setter_func)(DataType) = nullptr;
  DataType (TargetClass::*getter_func)() = nullptr;

  bool (TargetClass::*is_recall_enabled_func)() = nullptr;
  bool (TargetClass::*is_save_enabled_func)() = nullptr;
  void (TargetClass::*set_recall_enabled_func)(bool) = nullptr;
  void (TargetClass::*set_save_enabled_func)(bool) = nullptr;

  bool recall_enabled = true;
  bool save_enabled = true;

  SaveableSetting(
    const char* lbl,
    const char* category,
    TargetClass* tgt,
    DataType* var,
    bool* variable_recall_enabled = nullptr,
    bool* variable_save_enabled = nullptr,
    void (TargetClass::*setter)(DataType) = nullptr,
    DataType (TargetClass::*getter)() = nullptr,
    bool (TargetClass::*is_recall_enabled)() = nullptr,
    bool (TargetClass::*is_save_enabled)() = nullptr,
    void (TargetClass::*set_recall_enabled)(bool) = nullptr,
    void (TargetClass::*set_save_enabled)(bool) = nullptr
  ) {
    set_label(lbl);
    set_category(category);
    target = tgt;
    variable = var;
    setter_func = setter;
    getter_func = getter;
    is_recall_enabled_func = is_recall_enabled;
    is_save_enabled_func = is_save_enabled;
    set_recall_enabled_func = set_recall_enabled;
    set_save_enabled_func = set_save_enabled;

    if (variable_recall_enabled != nullptr) recall_enabled = *variable_recall_enabled;
    if (variable_save_enabled != nullptr) save_enabled = *variable_save_enabled;
  }

  const char* get_line() override {
    DataType v{};
    if (getter_func && target) v = (target->*getter_func)();
    else if (variable) v = *variable;
    sl_format_to_buf(linebuf, sizeof(linebuf), label, v);
    return linebuf;
  }

  bool parse_key_value(const char* key, const char* value) override {
    if (strcmp(key, label) != 0) return false;

    bool can_recall = recall_enabled;
    if (is_recall_enabled_func && target) can_recall = (target->*is_recall_enabled_func)();
    if (!can_recall) return false;

    DataType parsed = sl_parse_from_cstr<DataType>(value);

    if (setter_func && target) {
      (target->*setter_func)(parsed);
      return true;
    } else if (variable) {
      *variable = parsed;
      return true;
    }
    return false;
  }

  virtual size_t heap_size() const override { return sizeof(SaveableSetting<TargetClass, DataType>); }

  void set_recall_enabled(bool s) {
    if (set_recall_enabled_func && target) (target->*set_recall_enabled_func)(s);
    else recall_enabled = s;
  }
  void set_save_enabled(bool s) {
    if (set_save_enabled_func && target) (target->*set_save_enabled_func)(s);
    else save_enabled = s;
  }
};

// -------------------- LSaveableSetting (callable-based, uses vl::Func) --------------------
template<typename DataType>
class LSaveableSetting : public SaveableSettingBase {
public:
  using setter_func_t      = vl::Func<void(DataType)>;
  using getter_func_t      = vl::Func<DataType()>;

  setter_func_t     setter;
  getter_func_t     getter;

  DataType* variable = nullptr;
  bool recall_enabled = true;
  bool save_enabled = true;

  LSaveableSetting(
    const char* lbl,
    const char* category,
    DataType* var,
    setter_func_t     setter_callable    = {},
    getter_func_t     getter_callable    = {}
  ) {
    set_label(lbl);
    set_category(category);
    variable      = var;
    setter        = setter_callable;
    getter        = getter_callable;
  }

  const char* get_line() override {
    DataType v{};
    if (sl_callable_valid(getter))  v = getter();
    else if (variable) v = *variable;
    sl_format_to_buf(linebuf, sizeof(linebuf), label, v);
    return linebuf;
  }

  bool parse_key_value(const char* key, const char* value) override {
    if (strcmp(key, label) != 0) return false;
    if (!recall_enabled) return false;

    DataType parsed = sl_parse_from_cstr<DataType>(value);

    if (setter)        { setter(parsed); return true; }
    if (variable)      { *variable = parsed; return true; }

    return false;
  }

  virtual size_t heap_size() const override { return sizeof(LSaveableSetting<DataType>); }

  void set_recall_enabled_fn(bool s) { recall_enabled = s; }
  void set_save_enabled_fn(bool s)   { save_enabled = s; }
};