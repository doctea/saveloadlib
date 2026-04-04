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

// Generic parser helper
template<typename T>
static inline T sl_parse_from_cstr(const char* s) {
  if constexpr (sl_is_same<T, bool>::value) {
    return (atoi(s) != 0);
  } else if constexpr (sl_is_integral<T>::value) {
    return (T)atoll(s);
  } else if constexpr (sl_is_floating_point<T>::value) {
    return (T)atof(s);
  } else {
    static_assert(!sl_is_same<T, T>::value, "No generic parser for this type; specialise or implement custom setting");
  }
}


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
    label = lbl;
    category_name = category;
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

    if constexpr (sl_is_same<DataType, bool>::value) {
      snprintf(linebuf, sizeof(linebuf), "%s=%d", label, v ? 1 : 0);
    } else if constexpr (sl_is_integral<DataType>::value) {
      snprintf(linebuf, sizeof(linebuf), "%s=%lld", label, (long long)v);
    } else if constexpr (sl_is_floating_point<DataType>::value) {
      snprintf(linebuf, sizeof(linebuf), "%s=%.6g", label, (double)v);
    } else {
      snprintf(linebuf, sizeof(linebuf), "%s=0", label);
    }
    return linebuf;
  }

  bool parse_key_value(const char* key, const char* value) override {
    sl_trim_inplace(value); // remove whitespace and CR/LF which may be present in file-based loading
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
  using is_recall_func_t   = vl::Func<bool()>;
  using is_save_func_t     = vl::Func<bool()>;
  using set_recall_func_t  = vl::Func<void(bool)>;
  using set_save_func_t    = vl::Func<void(bool)>;

  setter_func_t     setter;
  getter_func_t     getter;
  is_recall_func_t  is_recall;
  is_save_func_t    is_save;
  set_recall_func_t set_recall;
  set_save_func_t   set_save;

  DataType* variable = nullptr;
  bool recall_enabled = true;
  bool save_enabled = true;

  LSaveableSetting(
    const char* lbl,
    const char* category,
    DataType* var,
    setter_func_t     setter_callable    = {},
    getter_func_t     getter_callable    = {},
    is_recall_func_t  is_recall_callable = {},
    is_save_func_t    is_save_callable   = {},
    set_recall_func_t set_recall_callable = {},
    set_save_func_t   set_save_callable  = {}
  ) {
    label         = lbl;
    category_name = category;
    variable      = var;
    setter        = setter_callable;
    getter        = getter_callable;
    is_recall     = is_recall_callable;
    is_save       = is_save_callable;
    set_recall    = set_recall_callable;
    set_save      = set_save_callable;
  }

  const char* get_line() override {
    DataType v{};
    if (sl_callable_valid(getter))  v = getter();
    else if (variable) v = *variable;

    if constexpr (sl_is_same<DataType, bool>::value) {
      snprintf(linebuf, sizeof(linebuf), "%s=%d", label, v ? 1 : 0);
    } else if constexpr (sl_is_integral<DataType>::value) {
      snprintf(linebuf, sizeof(linebuf), "%s=%lld", label, (long long)v);
    } else if constexpr (sl_is_floating_point<DataType>::value) {
      snprintf(linebuf, sizeof(linebuf), "%s=%.6g", label, (double)v);
    } else {
      snprintf(linebuf, sizeof(linebuf), "%s=0", label);
    }
    return linebuf;
  }

  bool parse_key_value(const char* key, const char* value) override {
    if (strcmp(key, label) != 0) return false;

    bool can_recall = recall_enabled;
    if (is_recall) can_recall = is_recall();
    if (!can_recall) return false;

    DataType parsed = sl_parse_from_cstr<DataType>(value);

    if (setter)        { setter(parsed); return true; }
    if (variable)      { *variable = parsed; return true; }

    return false;
  }

  void set_recall_enabled_fn(bool s) {
    if (set_recall) { set_recall(s); return; }
    recall_enabled = s;
  }
  void set_save_enabled_fn(bool s) {
    if (set_save) { set_save(s); return; }
    save_enabled = s;
  }
};