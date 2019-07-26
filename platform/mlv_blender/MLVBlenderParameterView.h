#ifndef _MLVBlenderParameterView_h_
#define _MLVBlenderParameterView_h_


class MLVBlenderParameterView;

#include "MLVBlenderGUI.h"


class MLVBlenderParameterView : public Gtk::TreeView
{
public:
    MLVBlenderParameterView();

    void SetMLVBlender(MLVBlender_t * Blender);
    void SetParent(MLVBlenderGUI * Parent);

    void UpdateMLVBlender();

    void UpdateFromMLVBlender(MLVBlender_t * MLVBlender);


private:
    MLVBlender_t * blender;
    MLVBlenderGUI * parent;

    bool listen_to_change = true;

    void MyChangedSignal(const Gtk::TreeModel::Path& path, const Gtk::TreeModel::iterator& iter);

    class ModelColumns : public Gtk::TreeModel::ColumnRecord
    {
        public:

        ModelColumns()
        { add(col_file_name); add(col_offset_x); add(col_offset_y); add(col_crop_left);
        add(col_crop_right); add(col_crop_bottom); add(col_crop_top); add(col_exposure); }

        Gtk::TreeModelColumn<Glib::ustring> col_file_name;
        Gtk::TreeModelColumn<int> col_offset_x;
        Gtk::TreeModelColumn<int> col_offset_y;
        Gtk::TreeModelColumn<int> col_crop_left;
        Gtk::TreeModelColumn<int> col_crop_right;
        Gtk::TreeModelColumn<int> col_crop_bottom;
        Gtk::TreeModelColumn<int> col_crop_top;
        Gtk::TreeModelColumn<float> col_exposure;
    };

    ModelColumns columns;

    Glib::RefPtr<Gtk::ListStore> m_refTreeModel;  
};


#endif