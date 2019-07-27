#ifndef _MLVBlenderGUI_h_
#define _MLVBlenderGUI_h_

class MLVBlenderGUI;

#include "MLVBlender.h"
#include "MLVBlenderParameterView.h"
#include <epoxy/gl.h>

class MLVBlenderGUI : public Gtk::Window
{
public:
    MLVBlenderGUI();

    void UpdateInterface();

    bool on_delete_event(GdkEventAny * any_event) override;

    void GLArea_realize();
    void GLArea_unrealize();
    bool GLArea_render(const Glib::RefPtr<Gdk::GLContext>& /* context */);

private:
    MLVBlender_t blender[1];

    Gtk::HeaderBar header_bar;
    Gtk::Button add_file_button;
    Gtk::Button export_button;

    Gtk::VPaned mainview;

    MLVBlenderParameterView * MLV_parameters;

    Gtk::GLArea image_view_gl;

    bool image_changed; /* Flag for OpenGL to update image */
    GLuint vbo;
    GLuint vao;
    GLuint ebo;
    GLuint shaderProgram;
    GLuint textures[2];
};

#endif