/**
 * 李震
 * 我的码云：https://git.oschina.net/git-lizhen
 * 我的CSDN博客：http://blog.csdn.net/weixin_38215395
 * 联系：QQ1039953685
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QPaintEvent>
#include <QWidget>
#include <QtDebug>

#include <QtConcurrent/qtconcurrentrun.h>
#include "videoplayer.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

	//QImage Indentificate(QImage img);    //显示二值图像，2017.10.10

protected:
    //void paintEvent(QPaintEvent *event);

private:
    Ui::MainWindow *ui;

    VideoPlayer *mPlayer;                  //播放线程

    QImage mImage;                         //记录当前的图像
    QPixmap  mCachedImage;   // 缓存缩放后的图像
    QImage R_mImage;                       //2017.8.11---lizhen

    QString url; 

    bool open_red=false;

private slots:
    void slotGetRFrame(QImage img);        //2017.8.11---lizhen
    bool slotOpenRed();                    //2017.8.12---lizhen
    bool slotCloseRed();                   //2017.8.12
    // 声明槽函数（用于接收视频帧）
    void slotGetOneFrame(QImage img);

    // 声明更新视频标签的函数
    void updateVideoLabel();
    void onPullStreamClicked(bool checked);
    void onStreamError(const QString &errorMsg); // 新增错误处理槽
    void on_pushstreamButton_clicked(bool checked);
    void onPushButtonReset();  // 按钮状态复位
};

#endif // MAINWINDOW_H
