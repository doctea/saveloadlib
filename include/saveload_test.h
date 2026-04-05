// a test object for making it easier to test save/load functionality
// we want this to act roughly like one of our real project objects, but with a simple set of settings that we can easily manipulate and verify
// so we want to be able to use Parameters of a couple of types, and also some simple raw variables, to make sure both of those work properly with the save/load system
// and we want to provide a mymenu UI for it so we can manually adjust things and test
// also be good to maybe have it automate the process of changing some values, saving, loading, and verifying that the loaded values match what we expect

#pragma once

#include "saveload_settings.h"

#include "parameters/Parameter.h"
#include "parameters/ProxyParameter.h"

#include "ParameterManager.h"

#include "mymenu_items/ParameterMenuItems_lowmemory.h"

#include "functional-vlpp.h"

class TestSaveableObject : public SHStorage<0, 4> {  // 4 settings; no children
    public:
    int int_value = 0;
    float float_value = 0.0;
    float float_proxy_value = 0.0;
    bool bool_value = false;
    char string_value[20] = "default";

    // lambda callbacks to trigger save/loads
    vl::Func<void()> save_callback;
    vl::Func<void()> load_callback;

    LinkedList<FloatParameter*> *parameters = nullptr;

    TestSaveableObject(vl::Func<void()> save_callback = {}, vl::Func<void()> load_callback = {}) 
            : save_callback(save_callback), load_callback(load_callback) 
    {
        this->set_path_segment("TestObject");
    }

    LinkedList<FloatParameter*> *get_parameters() override {
        if (!parameters) {
            parameters = new LinkedList<FloatParameter*>();
            parameters->add(new DataParameter<int>("int_value", &this->int_value));
            parameters->add(new DataParameter<float>("float_value", &this->float_value));
            parameters->add(new DataParameter<bool>("bool_value", &this->bool_value));
            parameters->add(new DataParameter<const char*>("string_value", &this->string_value[0]));
            parameters->add(new ProxyParameter<float>("float_proxy", [=]() -> float { return this->float_proxy_value; }, [=](float v) { this->float_proxy_value = v; }));

            parameter_manager->addParameters(parameters);
        }

        return parameters;
    }

    void setup_saveable_settings() override {
        /*this->register_setting(new LSaveableSetting<int>("int_value", "Integer Value", &this->int_value));
        this->register_setting(new LSaveableSetting<float>("float_value", "Float Value", &this->float_value));
        this->register_setting(new LSaveableSetting<bool>("bool_value", "Boolean Value", &this->bool_value));
        this->register_setting(new LSaveableSetting<const char*>("string_value", "String Value", &this->string_value[0]));*/
        this->register_setting(new LSaveableSetting<float>("float_proxy_value", "Float Proxy Value", &this->float_proxy_value));

        LinkedList<FloatParameter*> *parameters = this->get_parameters();
        for (int i = 0; i < parameters->size(); i++) {
            this->register_child(parameters->get(i));
        }
    }

    void create_menu_items() {
        menu->add_page("Test Object", C_CYAN);

        SubMenuItemBar_Columns *columns = new SubMenuItemBar_Columns("Settings", 3, true, true);

        columns->add(new LambdaNumberControl<int>("Int Value", [=](int v) { this->int_value = v; }, [=]() -> int { return this->int_value; }, nullptr, 0, 100));
        columns->add(new LambdaNumberControl<float>("Float Value", [=](float v) { this->float_value = v; }, [=]() -> float { return this->float_value; }, nullptr, 0.0f, 100.0f));
        columns->add(new LambdaToggleControl("Bool Value", [=](bool v) { this->bool_value = v; }, [=]() -> bool { return this->bool_value; }));
        columns->add(new LambdaTextControl("String Value", [=](const char* v) { strncpy(this->string_value, v, sizeof(this->string_value)); }, [=]() -> const char* { return this->string_value; }, 20));
        columns->add(new LambdaNumberControl<float>("Float Proxy Value", [=](float v) { this->float_proxy_value = v; }, [=]() -> float { return this->float_proxy_value; }, nullptr, 0.0f, 100.0f));

        menu->add(columns);

        menu->add(new LambdaActionConfirmItem("Save", [=]() { if (this->save_callback) this->save_callback(); }, false));
        menu->add(new LambdaActionConfirmItem("Load", [=]() { if (this->load_callback) this->load_callback(); }, false));

        create_low_memory_parameter_controls("Test Object Parameters", this->get_parameters(), C_CYAN);
    }
};