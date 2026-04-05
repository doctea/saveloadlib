// a test object for making it easier to test save/load functionality
// we want this to act roughly like one of our real project objects, but with a simple set of settings that we can easily manipulate and verify
// so we want to be able to use Parameters of a couple of types, and also some simple raw variables, to make sure both of those work properly with the save/load system
// and we want to provide a mymenu UI for it so we can manually adjust things and test
// also be good to maybe have it automate the process of changing some values, saving, loading, and verifying that the loaded values match what we expect

#pragma once

#ifdef ENABLE_TESTSAVELOAD

#include "functional-vlpp.h"
#include "LinkedList.h"

#include "saveload_settings.h"

#include "parameters/Parameter.h"
#include "parameters/ProxyParameter.h"

#include "ParameterManager.h"

#include "mymenu_items/ParameterMenuItems_lowmemory.h"

#include "menu.h"
#include "submenuitem_bar.h"

class TestSaveableObject : public SHStorage<0, 4> {  // 4 settings; no children
    public:
    int int_value = 0;
    float float_value = 0.0;
    float float_proxy_source = 0.0;
    float float_proxy_destination = 0.0;
    bool bool_value = false;

    int direct_int_value = 0; // 
    float direct_float_value = 0.0; // these are to test that we can save/load both raw variables and Parameters correctly
    bool direct_bool_value = false;

    // lambda callbacks to trigger save/loads
    vl::Func<void()> test_loadsave_callback = [=](void) -> void {
        Serial.println("test_loadsave_callback callback triggered");
        Serial.flush();
        // dump info to serial for verification
        Serial.printf("int_value: %d\n", this->int_value);
        Serial.printf("float_value: %f\n", this->float_value);
        Serial.printf("float_proxy_source: %f\n", this->float_proxy_source);
        Serial.printf("float_proxy_destination: %f\n", this->float_proxy_destination);
        Serial.printf("bool_value: %d\n", this->bool_value);

        // store the current settings to a LinkedList for later verification
        LinkedList<String> lines;
        sl_print_tree_with_lambda(
            this,
            [&lines](const char* line) { lines.add(String(line)); },
            1 // max depth 1 to just get settings, no children (this object has no children anyway)
        );

        // print the stored lines for verification
        Serial.println("Captured settings lines:");
        for (int i = 0; i < lines.size(); i++) {
            Serial.println(lines.get(i));
        }

        // randomise the settings to different values to make sure the load actually changes them back
        this->int_value = random(0, 100);
        this->float_value = random(0.0, 100.0);
        this->float_proxy_source = random(0.0, 100.0);
        this->float_proxy_destination = random(0.0, 100.0);
        this->bool_value = !this->bool_value;
        this->direct_int_value = random(0, 100);
        this->direct_float_value = random(0.0, 100.0);
        this->direct_bool_value = !this->direct_bool_value;

        // also change the parameters values, modulation, and ranges
        LinkedList<FloatParameter*> *params = this->get_parameters();
        for (int i = 0; i < params->size(); i++) {
            FloatParameter* p = params->get(i);
            for (int i = 0 ; i < 3 ; i++) { // todo: find the correct define to use for number of modulation slots
                p->connect_input(parameter_manager->getInputForIndex(random(0, parameter_manager->available_inputs->size())), random(0.0, 1.0));  // connect slot 0 to the first parameter input for testing
            }
            p->setRangeMinimumLimit(random(0.0, 0.25));
            p->setRangeMaximumLimit(random(0.75, 1.0));
        }

        // now reload the settings from the saved lines to verify that the save/load process works correctly
        sl_load_from_linkedlist("TestObject", lines);

        // now save again to a new list and compare
        LinkedList<String> lines_after_load;
        sl_print_tree_with_lambda(
            this,
            [&lines_after_load](const char* line) { lines_after_load.add(String(line)); },
            1 // max depth 1 to just get settings, no children (this object has no children anyway)
        );
        // print the new captured lines for verification        Serial.println("Captured settings lines after load:");
        for (int i = 0; i < lines_after_load.size(); i++) {
            Serial.println(lines_after_load.get(i));
        }
        // compare the two lists        
        bool match = true;
        if (lines.size() != lines_after_load.size()) {
            Serial.println("Mismatch in number of lines before save and after load!");
            match = false;
        } else {
            for (int i = 0; i < lines.size(); i++) {
                if (lines.get(i) != lines_after_load.get(i)) {
                    Serial.printf("Mismatch in line %d: before '%s' vs after '%s'\n", i, lines.get(i).c_str(), lines_after_load.get(i).c_str());
                    match = false;
                }
            }
        }
        if (match) {
            Serial.println("Success: settings lines match before save and after load!");
        } else {
            Serial.println("Failure: settings lines do not match before save and after load!");
        }
    };

    LinkedList<FloatParameter*> *parameters = nullptr;

    TestSaveableObject() {
        Serial.println("Constructing TestSaveableObject");
        this->set_path_segment("TestObject");
    }

    LinkedList<FloatParameter*> *get_parameters() {
        if (!parameters) {
            parameters = new LinkedList<FloatParameter*>();
            parameters->add(new LDataParameter<int>("int_value", [=](int v) { this->int_value = v; }, [=]() -> int { return this->int_value; }));
            parameters->add(new LDataParameter<float>("float_value", [=](float v) { this->float_value = v; }, [=]() -> float { return this->float_value; }));
            parameters->add(new LDataParameter<bool>("bool_value", [=](bool v) { this->bool_value = v; }, [=]() -> bool { return this->bool_value; }));

            parameters->add(new ProxyParameter<float>(
                "float_proxy", 
                &this->float_proxy_source,
                &this->float_proxy_destination,
                0.0f, 100.0f
            ));

            parameter_manager->addParameters(parameters);
        }

        return parameters;
    }

    void setup_saveable_settings() override {
        // this->register_setting(new LSaveableSetting<int>("int_value", "Integer Value", &this->int_value));
        // this->register_setting(new LSaveableSetting<float>("float_value", "Float Value", &this->float_value));
        // this->register_setting(new LSaveableSetting<bool>("bool_value", "Boolean Value", &this->bool_value));

        this->register_setting(new LSaveableSetting<int>("direct_int_value", "Direct Int Value", &this->direct_int_value));
        this->register_setting(new LSaveableSetting<float>("direct_float_value", "Direct Float Value", &this->direct_float_value));
        this->register_setting(new LSaveableSetting<bool>("direct_bool_value", "Direct Bool Value", &this->direct_bool_value));

        LinkedList<FloatParameter*> *parameters = this->get_parameters();
        for (int i = 0; i < parameters->size(); i++) {
            this->register_child(parameters->get(i));
        }
    }

    void create_menu_items() {
        menu->add_page("Test Object", RED);

        menu->add(new LambdaActionConfirmItem("Test SaveLoad", this->test_loadsave_callback));

        SubMenuItemColumns *columns = new SubMenuItemColumns("Parameters", 3, true, true);

        columns->add(new LambdaNumberControl<int>("Int Value", [=](int v) { this->int_value = v; }, [=]() -> int { return this->int_value; }, nullptr, 0, 100));
        columns->add(new LambdaNumberControl<float>("Float Value", [=](float v) { this->float_value = v; }, [=]() -> float { return this->float_value; }, nullptr, 0.0f, 100.0f));
        columns->add(new LambdaToggleControl("Bool Value", [=](bool v) { this->bool_value = v; }, [=]() -> bool { return this->bool_value; }));
        columns->add(new LambdaNumberControl<float>("Float Proxy Source", [=](float v) { this->float_proxy_source = v; }, [=]() -> float { return this->float_proxy_source; }, nullptr, 0.0f, 100.0f));
        columns->add(new LambdaNumberControl<float>("Float Proxy Dest", [=](float v) { this->float_proxy_destination = v; }, [=]() -> float { return this->float_proxy_destination; }, nullptr, 0.0f, 100.0f));

        create_low_memory_parameter_controls("Test Object Parameters", this->get_parameters(), RED);

        menu->add(columns);

        // now directs too
        columns = new SubMenuItemColumns("Direct Variables", 3, true, true);
        columns->add(new LambdaNumberControl<int>("Direct Int", [=](int v) { this->direct_int_value = v; }, [=]() -> int { return this->direct_int_value; }, nullptr, 0, 100));
        columns->add(new LambdaNumberControl<float>("Direct Float", [=](float v) { this->direct_float_value = v; }, [=]() -> float { return this->direct_float_value; }, nullptr, 0.0f, 100.0f));
        columns->add(new LambdaToggleControl("Direct Bool", [=](bool v) { this->direct_bool_value = v; }, [=]() -> bool { return this->direct_bool_value; }));
        menu->add(columns);
    }
};

extern TestSaveableObject* test_object;

#endif