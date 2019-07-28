// g++ main.cpp `pkg-config gtkmm-3.0 --cflags --libs`
#include <string>
#include <gtkmm.h>

/* Main parts of the program are here */
extern "C" {
#include "MLVBlender.h"
#include "../../src/mlv_include.h"
}

#include "MLVBlenderGUI.h"
#include "MLVBlenderParameterView.h"

// #include "RGBView.h"


GLuint textures[2];


// Shader sources
const GLchar* vertexSource = R"glsl(
#version 150 core
in vec2 position;
in vec3 color;
in vec2 texcoord;
out vec3 Color;
out vec2 Texcoord;
void main()
{
    Color = color;
    Texcoord = texcoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";

const GLchar* fragmentSource = R"glsl(
#version 150 core

in vec2 Texcoord;
uniform sampler2D texInterface;

void main()
{
    vec4 pixel = texture(texInterface, Texcoord);

    if (pixel.r > 1.0)
        gl_FragColor = vec4(0.933,0.157,0.265,1.0);
    else
        gl_FragColor = vec4(pixel.r*3/(1+pixel.r*3)*1.33);
}

)glsl";


MLVBlenderGUI::MLVBlenderGUI()
{
    init_MLVBlender(this->blender);

    // MLVBlenderAddMLV(this->blender, "/home/ilia/Videos/ExampleMLVs/100EOS5D/M26-0027.MLV");
    // MLVBlenderAddMLV(this->blender, "/home/ilia/Videos/ExampleMLVs/100EOS5D/M26-0028.MLV");

    // MLVBlenderBlend(this->blender, 1);


    this->MLV_parameters = new MLVBlenderParameterView();
    MLV_parameters->SetParent(this);
    MLV_parameters->SetMLVBlender(this->blender);

    this->header_bar.pack_start(this->add_file_button);
    this->add_file_button.set_label("Open MLV");
    this->add_file_button.signal_button_release_event().connect([&](GdkEventButton * event) {
        Gtk::FileChooserDialog dialog("Choose an MLV file", Gtk::FILE_CHOOSER_ACTION_OPEN);
        //Add response buttons the the dialog:
        dialog.set_select_multiple(true);
        dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
        dialog.add_button("Open", Gtk::RESPONSE_OK);

        int result = dialog.run();

        std::vector<std::string> chosen_files = dialog.get_filenames();
        for (int i = 0; i < chosen_files.size(); ++i)
        {
            MLVBlenderAddMLV(this->blender, chosen_files[i].c_str());
        }

        this->UpdateInterface();
        return true;
    });
    puts("hi");

    this->header_bar.pack_end(this->export_button);
    this->export_button.set_label("Export");
    this->export_button.signal_button_release_event().connect([&](GdkEventButton * event) {
        Gtk::FileChooserDialog dialog("Choose an MLV file", Gtk::FILE_CHOOSER_ACTION_SAVE);
        dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
        dialog.add_button("Save", Gtk::RESPONSE_OK);
        int result = dialog.run();
        std::string saved_path = dialog.get_filename();
        MLVBlenderExportMLV(blender, saved_path.c_str());
        // this->UpdateInterface();
        return true;
    });

    this->set_titlebar(this->header_bar);
    this->header_bar.set_show_close_button(true);
    this->set_default_size(850,600);
    this->add(this->mainview);

    // this->mainview.add2(this->tabbed_view);
    this->mainview.add1(*this->MLV_parameters);
    // this->mainview.add2(this->image_view);
    this->image_view_gl.set_hexpand(true);
    this->image_view_gl.set_vexpand(true);
    this->image_view_gl.set_auto_render(true);
    // Connect gl area signals
    this->image_view_gl.signal_realize().connect(sigc::mem_fun(*this, &MLVBlenderGUI::GLArea_realize));
    // Important that the unrealize signal calls our handler to clean up
    // GL resources _before_ the default unrealize handler is called (the "false")
    this->image_view_gl.signal_unrealize().connect(sigc::mem_fun(*this, &MLVBlenderGUI::GLArea_unrealize), false);
    this->image_view_gl.signal_render().connect(sigc::mem_fun(*this, &MLVBlenderGUI::GLArea_render), false);
    this->mainview.add2(this->image_view_gl);

    this->MLV_parameters->UpdateFromMLVBlender(blender);

    this->set_title("MLV Stitcher");
    this->show_all();
}

void MLVBlenderGUI::UpdateInterface()
{
    MLV_parameters->UpdateFromMLVBlender(blender);

    MLVBlenderBlend(blender, 1);

    char subtitle_string[256];
    sprintf(subtitle_string, "Result %ix%i", MLVBlenderGetOutputWidth(blender), MLVBlenderGetOutputHeight(blender));
    header_bar.set_subtitle(subtitle_string);

    image_changed = true;
    image_view_gl.queue_draw();
}

bool MLVBlenderGUI::on_delete_event(GdkEventAny * any_event)
{
    Gtk::MessageDialog dialog(*this, "Are you sure you want exit?", true, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
    dialog.set_title("Close Form");
    dialog.set_modal();
    dialog.set_position(Gtk::WindowPosition::WIN_POS_CENTER);
    if (dialog.run() == Gtk::RESPONSE_YES)
        return this->Window::on_delete_event(any_event);
    return true;
}

void MLVBlenderGUI::GLArea_realize()
{
    image_view_gl.make_current();
    image_view_gl.throw_if_error();

    // Create Vertex Array Object
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Create a Vertex Buffer Object and copy the vertex data to it
    glGenBuffers(1, &vbo);

    GLfloat vertices[] = {
    //  Position      Color             Texcoords
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, // Top-left
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, // Top-right
         1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, // Bottom-right
        -1.0f,  1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f  // Bottom-left
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Create an element array
    glGenBuffers(1, &ebo);

    GLuint elements[] = {
        0, 1, 2,
        2, 3, 0
    };

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elements), elements, GL_STATIC_DRAW);

    // Create and compile the vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, NULL);
    glCompileShader(vertexShader);

    // Create and compile the fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
    glCompileShader(fragmentShader);

    // Link the vertex and fragment shader into a shader program
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glBindFragDataLocation(shaderProgram, 0, "outColor");
    glLinkProgram(shaderProgram);
    glUseProgram(shaderProgram);

    // Specify the layout of the vertex data
    GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(GLfloat), 0);

    GLint colAttrib = glGetAttribLocation(shaderProgram, "color");
    glEnableVertexAttribArray(colAttrib);
    glVertexAttribPointer(colAttrib, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

    GLint texAttrib = glGetAttribLocation(shaderProgram, "texcoord");
    glEnableVertexAttribArray(texAttrib);
    glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(GLfloat), (void*)(5 * sizeof(GLfloat)));

    // Load textures
    glGenTextures(2, textures);

    int width, height;
    unsigned char * image;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);

    float pixels[] = {
        0.1,0.1,0.1,1, 0.1,0.1,0.1,1, 0.1,0.1,0.1,1,
        0.1,0.1,0.1,1, 0.1,0.1,0.1,1, 0.1,0.1,0.1,1,
        0.1,0.1,0.1,1, 0.1,0.1,0.1,1, 0.1,0.1,0.1,1
    };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 3, 3, 0, GL_RGBA, GL_FLOAT, pixels);
    glUniform1i(glGetUniformLocation(shaderProgram, "texInterface"), 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void MLVBlenderGUI::GLArea_unrealize()
{
    image_view_gl.make_current();

    image_view_gl.throw_if_error();

    // Delete buffers and program
    glDeleteBuffers(1, &vao);
    glDeleteProgram(shaderProgram);
}

bool MLVBlenderGUI::GLArea_render(const Glib::RefPtr<Gdk::GLContext>& /* context */)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (image_changed == true)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, MLVBlenderGetOutputWidth(blender), MLVBlenderGetOutputHeight(blender), 0, GL_RED, GL_FLOAT, MLVBlenderGetOutput(blender));
        image_changed = false;
    }

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glFlush();

    return true;
}

int main(int argc, char* argv[])
{
    Glib::RefPtr<Gtk::Application> application = Gtk::Application::create(argc, argv, "org.MLVApp.MLVStitch");
    MLVBlenderGUI main_window;
    return application->run(main_window);
}