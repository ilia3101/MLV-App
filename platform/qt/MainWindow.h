#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QLabel>
#include <QFileDialog>
#include <QDebug>
#include <QTimerEvent>
#include <QResizeEvent>
#include <QFileOpenEvent>
#include "../../src/mlv_include.h"
#include "InfoDialog.h"
#include "StatusDialog.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    void timerEvent( QTimerEvent *t );
    void resizeEvent( QResizeEvent *event );
    bool event( QEvent *event );

private slots:
    void on_actionOpen_triggered();
    void on_actionAbout_triggered();
    void on_horizontalSliderPosition_valueChanged(void);
    void on_checkBoxUseAmaze_toggled(bool checked);
    void on_actionClip_Information_triggered();
    void on_horizontalSliderExposure_valueChanged(int position);
    void on_horizontalSliderTemperature_valueChanged(int position);
    void on_horizontalSliderTint_valueChanged(int position);
    void on_horizontalSliderSaturation_valueChanged(int position);
    void on_horizontalSliderDS_valueChanged(int position);
    void on_horizontalSliderDR_valueChanged(int position);
    void on_horizontalSliderLS_valueChanged(int position);
    void on_horizontalSliderLR_valueChanged(int position);
    void on_horizontalSliderLighten_valueChanged(int position);
    void on_actionGoto_First_Frame_triggered();
    void on_actionExport_triggered();

    void on_checkBoxHighLightReconstruction_toggled(bool checked);

private:
    Ui::MainWindow *ui;
    InfoDialog *m_pInfoDialog;
    StatusDialog *m_pStatusDialog;
    mlvObject_t *m_pMlvObject;
    processingObject_t *m_pProcessingObject;
    QLabel *m_pRawImageLabel;
    QLabel *m_pCachingStatus;
    QLabel *m_pFpsStatus;
    uint8_t *m_pRawImage;
    bool m_frameChanged;
    int m_currentFrameIndex;
    bool m_dontDraw;
    bool m_frameStillDrawing;
    bool m_fileLoaded;
    int m_timerId;
    int m_timerCacheId;
    QString m_lastSaveFileName;
    void drawFrame();
    void openMlv( QString fileName );
};

#endif // MAINWINDOW_H
