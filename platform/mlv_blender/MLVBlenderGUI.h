#ifndef _MLVBlenderGUI_h_
#define _MLVBlenderGUI_h_

class MLVBlenderGUI;

#include "MLVBlender.h"
#include "MLVBlenderParameterView.h"

class MLVBlenderGUI : public Gtk::Window
{
public:
    MLVBlenderGUI();

    void UpdateInterface();

    bool on_delete_event(GdkEventAny * any_event) override;

private:
    MLVBlender_t blender[1];

    Gtk::HeaderBar header_bar;
    Gtk::Button add_file_button;
    Gtk::Button export_button;

    Gtk::VPaned mainview;

    MLVBlenderParameterView * MLV_parameters;

    Gtk::Image image_view;
};

#endif