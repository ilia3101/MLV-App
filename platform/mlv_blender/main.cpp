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

    this->header_bar.pack_end(this->export_button);
    this->export_button.set_label("Export");
    this->export_button.signal_button_release_event().connect([&](GdkEventButton * event) {
        // for (int i = 0; i < 100) {
        //     return true;
        // }
        MLVBlenderExportMLV(blender, "out.mlv");
        // this->UpdateInterface();
        return true;
    });

    this->set_titlebar(this->header_bar);
    this->header_bar.set_show_close_button(true);
    this->set_default_size(850,600);
    this->add(this->mainview);

    // this->mainview.add2(this->tabbed_view);
    this->mainview.add1(*this->MLV_parameters);
    this->mainview.add2(this->image_view);

    this->MLV_parameters->UpdateFromMLVBlender(blender);

    this->set_title("MLV Stitcher");
    this->show_all();
}

void MLVBlenderGUI::UpdateInterface()
{
    MLV_parameters->UpdateFromMLVBlender(blender);
    // Gdk::Pixbuf pixbuffer();
    // puts("hihihi");

    MLVBlenderBlend(blender, 1);

    Glib::RefPtr<Gdk::Pixbuf> gdkpixbuffer = Gdk::Pixbuf::create_from_data(
        (uint8_t *)MLVBlenderGetOutput(blender),
        Gdk::COLORSPACE_RGB, false, 8,
        MLVBlenderGetOutputWidth(blender),
        MLVBlenderGetOutputHeight(blender),
        MLVBlenderGetOutputWidth(blender)*3
    );

    image_view.set("output.bmp");

    char gjkhkjh[256];
    sprintf(gjkhkjh, "Result %ix%i", MLVBlenderGetOutputWidth(blender), MLVBlenderGetOutputHeight(blender));
    header_bar.set_subtitle(gjkhkjh);
    // gdkpixbuffer;
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

int main(int argc, char* argv[])
{

    Glib::RefPtr<Gtk::Application> application = Gtk::Application::create(argc, argv, "org.ilia3101.MLVComposite");
    MLVBlenderGUI main_window;
    return application->run(main_window);
}