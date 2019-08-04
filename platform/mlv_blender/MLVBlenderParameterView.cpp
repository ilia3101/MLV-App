#include <gtkmm.h>

extern "C" {
#include <stdio.h>
#include "MLVBlender.h"
#include "../../src/mlv_include.h"
}

#include "MLVBlenderParameterView.h"

MLVBlenderParameterView::MLVBlenderParameterView() : Gtk::TreeView()
{
    this->m_refTreeModel = Gtk::ListStore::create(this->columns);
    m_refTreeModel->signal_row_changed().connect(sigc::mem_fun(*this, &MLVBlenderParameterView::MyChangedSignal));
    this->set_model(this->m_refTreeModel);
    this->append_column("file", this->columns.col_file_name);
    this->append_column_editable("Visible", this->columns.col_visible);
    this->append_column_editable("Difference", this->columns.col_difference_blend);
    this->append_column_editable("Offset X", this->columns.col_offset_x);
    this->append_column_editable("Offset Y", this->columns.col_offset_y);
    this->append_column_editable("Crop left", this->columns.col_crop_left);
    this->append_column_editable("Crop right", this->columns.col_crop_right);
    this->append_column_editable("Crop bottom", this->columns.col_crop_bottom);
    this->append_column_editable("Crop top", this->columns.col_crop_top);
    this->append_column_editable("Feather left", this->columns.col_feather_left);
    this->append_column_editable("Feather right", this->columns.col_feather_right);
    this->append_column_editable("Feather bottom", this->columns.col_feather_bottom);
    this->append_column_editable("Feather top", this->columns.col_feather_top);
    this->append_column_editable("Exposure", this->columns.col_exposure);
}


void MLVBlenderParameterView::MyChangedSignal(const Gtk::TreeModel::Path& path, const Gtk::TreeModel::iterator& iter)
{
    char pathstr[20];
    sprintf(pathstr, "%s\n", path.to_string().c_str());
    printf("path: %s\n", path.to_string().c_str());

    int i;
    sscanf(pathstr, "%i", &i);

    Gtk::TreeModel::Row row = *iter;
    printf("pat2h: %i\n",  (int)row[columns.col_offset_y]);

    if (listen_to_change)
    {
        MLVBlenderSetMLVVisible(blender, i, (bool)row[columns.col_visible]);
        MLVBlenderSetMLVDifferenceBlending(blender, i, (bool)row[columns.col_difference_blend]);
        MLVBlenderSetMLVOffsetX(blender, i, (int)row[columns.col_offset_x]);
        MLVBlenderSetMLVOffsetY(blender, i, (int)row[columns.col_offset_y]);
        MLVBlenderSetMLVCropLeft(blender, i, (int)row[columns.col_crop_left]);
        MLVBlenderSetMLVCropRight(blender, i, (int)row[columns.col_crop_right]);
        MLVBlenderSetMLVCropBottom(blender, i, (int)row[columns.col_crop_bottom]);
        MLVBlenderSetMLVCropTop(blender, i, (int)row[columns.col_crop_top]);
        MLVBlenderSetMLVFeatherLeft(blender, i, (int)row[columns.col_feather_left]);
        MLVBlenderSetMLVFeatherRight(blender, i, (int)row[columns.col_feather_right]);
        MLVBlenderSetMLVFeatherBottom(blender, i, (int)row[columns.col_feather_bottom]);
        MLVBlenderSetMLVFeatherTop(blender, i, (int)row[columns.col_feather_top]);
        MLVBlenderSetMLVExposure(blender, i, (float)row[columns.col_exposure]);
        parent->UpdateInterface();
    }
}

void MLVBlenderParameterView::SetMLVBlender(MLVBlender_t * Blender) {this->blender = Blender;}
void MLVBlenderParameterView::SetParent(MLVBlenderGUI * Parent) {this->parent = Parent;}


void MLVBlenderParameterView::UpdateFromMLVBlender(MLVBlender_t * MLVBlender)
{
    listen_to_change = false;
    m_refTreeModel->clear();
    int num_mlvs = MLVBlenderGetNumMLVs(MLVBlender);
    for (int i = 0; i < num_mlvs; ++i)
    {
        Gtk::TreeModel::Row row = *(m_refTreeModel->append());
        row[columns.col_file_name] = MLVBlenderGetMLVFileName(MLVBlender, i);
        row[columns.col_visible] = (bool)MLVBlenderGetMLVVisible(MLVBlender, i);
        row[columns.col_difference_blend] = (bool)MLVBlenderGetMLVDifferenceBlending(MLVBlender, i);
        row[columns.col_offset_x] = MLVBlenderGetMLVOffsetX(MLVBlender, i);
        row[columns.col_offset_y] = MLVBlenderGetMLVOffsetY(MLVBlender, i);
        row[columns.col_crop_left] = MLVBlenderGetMLVCropLeft(MLVBlender, i);
        row[columns.col_crop_right] = MLVBlenderGetMLVCropRight(MLVBlender, i);
        row[columns.col_crop_bottom] = MLVBlenderGetMLVCropBottom(MLVBlender, i);
        row[columns.col_crop_top] = MLVBlenderGetMLVCropTop(MLVBlender, i);
        row[columns.col_feather_left] = MLVBlenderGetMLVFeatherLeft(MLVBlender, i);
        row[columns.col_feather_right] = MLVBlenderGetMLVFeatherRight(MLVBlender, i);
        row[columns.col_feather_bottom] = MLVBlenderGetMLVFeatherBottom(MLVBlender, i);
        row[columns.col_feather_top] = MLVBlenderGetMLVFeatherTop(MLVBlender, i);
        row[columns.col_exposure] = MLVBlenderGetMLVExposure(MLVBlender, i);
    }
    listen_to_change = true;
}