/**
 * 李震
 * 我的码云：https://git.oschina.net/git-lizhen
 * 我的CSDN博客：http://blog.csdn.net/weixin_38215395
 * 联系：QQ1039953685
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QThread>
#include <QPainter>
#include <QInputDialog>
#include <QtMath>
#include <QtWidgets/QMessageBox>
#include <QTimer>

#include<iostream>
using namespace std;

#define APP_VERSION "1.0.0"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
   ui(new Ui::MainWindow)
{ 
	
    ui->setupUi(this);
    ui->comboBox->setCurrentIndex(0); // 默认选中 TCP
    QString title = QString("RTSP tool - Version %1, build:%2-%3").arg(APP_VERSION).arg(__DATE__).arg(__TIME__);
    this->setWindowTitle(title);
    mPlayer = new VideoPlayer;
    this->setWindowFlags(Qt::Widget | Qt::MSWindowsFixedSizeDialogHint);
    connect(mPlayer, &VideoPlayer::sig_GetOneFrame, this, &MainWindow::slotGetOneFrame);
    connect(ui->pullstreamButton, &QPushButton::toggled, this, &MainWindow::onPullStreamClicked);
    connect(mPlayer, &VideoPlayer::sig_GetRFrame, this, &MainWindow::slotGetRFrame);
    //2017.8.12---lizhen
    connect(ui->Open_red,&QAction::triggered,this,&MainWindow::slotOpenRed);
    connect(ui->Close_Red,&QAction::triggered,this,&MainWindow::slotCloseRed);
    connect(mPlayer, &VideoPlayer::sig_StreamError, this, &MainWindow::onStreamError);
    // mainwindow.cpp
    connect(mPlayer, &VideoPlayer::sig_RequireButtonReset, this, &MainWindow::onPushButtonReset);

    //mPlayer->startPlay();

}

MainWindow::~MainWindow()
{
    delete ui;
}

//// mainwindow.cpp
//void MainWindow::slotGetOneFrame(QImage img) {
//    mImage = img;
//    mCachedImage = QImage(); // 清空缓存，下次paintEvent重新缩放
//    update();
//}
//2017.10.10，显示二值图像
QImage Indentificate(QImage image)
{
	QSize size = image.size();
	QImage binaryImage(size, QImage::Format_RGB32);
	int width = image.width(), height = image.height();
	int threshold = 200;
	for (int i = 0; i<width; i++)
		for (int j = 0; j < height; j++)
		{
			QRgb pixel = image.pixel(i, j);
			int r = qRed(pixel) * 0.3;
			int g = qGreen(pixel) * 0.59;
			int b = qBlue(pixel) * 0.11;
			int rgb = r + g + b;        //先把图像灰度化，转化为灰度图像
			if (rgb > threshold)        //然后按某一阀值进行二值化
				rgb = 255;
			else
				rgb = 0;
			QRgb newPixel = qRgb(rgb, rgb, rgb);
			binaryImage.setPixel(i, j, newPixel);
		}
	return binaryImage;
}
//小窗口显示
void MainWindow::slotGetRFrame(QImage img)
{
    R_mImage = img;
	//2017.10.9---在新线程中执行显示二值图像函数
	//QFuture<QImage> future = QtConcurrent::run(Indentificate, mImage);
	//R_mImage = future.result();
    update(); //调用update将执行 paintEvent函数
}
//显示图像红色通道,2017.8.12---lizhen
bool MainWindow::slotOpenRed()
{
    open_red=true;
    return open_red;
}
//关闭图像红色通道，2017.8.12
bool MainWindow::slotCloseRed()
{
    open_red=false;
    return open_red;
}

// videoplayer.cpp
void VideoPlayer::playAudioData(const QByteArray &audioData)
{
    if (mAudioIO) {
        mAudioIO->write(audioData);
    }
}

//void MainWindow::paintEvent(QPaintEvent*) {
//    QPainter painter(this);
//    painter.fillRect(rect(), Qt::black);

//    if (!mImage.isNull()) {
//        if (mCachedImage.isNull()) {
//            mCachedImage = mImage.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
//        }
//        painter.drawImage((width() - mCachedImage.width())/2,
//                         (height() - mCachedImage.height())/2,
//                         mCachedImage);
//    }
//}
// 修改slot函数
void MainWindow::slotGetOneFrame(QImage img)
{
    mCachedImage = QPixmap::fromImage(img);
    updateVideoLabel();
}

void MainWindow::updateVideoLabel()
{
    QSize labelSize = ui->videoLabel->size();
    ui->videoLabel->setPixmap(mCachedImage.scaled(
        labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// 新增槽函数
// 修改拉流按钮的槽函数
// 修改后的槽函数
void MainWindow::onPullStreamClicked(bool checked)
{
    if (checked) {
        // 开始拉流
        QString rtspUrl = ui->pullEdit->text().trimmed();
        if (rtspUrl.isEmpty()) {
            QMessageBox::warning(this, "输入错误", "RTSP URL不能为空！");
            ui->pullstreamButton->setChecked(false);
            return;
        }

        // 获取用户选择的协议 (TCP/UDP)
        QString transport = ui->comboBox->currentText().toLower();
        ui->pullstreamButton->setText("停止拉流");
        mPlayer->stopPlay();
        mPlayer->setStreamUrl(rtspUrl);
        mPlayer->setTransportProtocol(transport); // 设置传输协议
        mPlayer->startPlay();
    } else {
        // 停止拉流
        ui->pullstreamButton->setText("开始拉流");
        mPlayer->stopPlay();
    }
}

// 错误处理槽函数
void MainWindow::onStreamError(const QString &errorMsg) {
    QMessageBox::critical(this, "Stream Error", errorMsg);
}

void MainWindow::on_pushstreamButton_clicked(bool checked) {
    QString inputUrl = ui->pushEdit->text().trimmed();
    QString outputUrl = ui->pushEdit_2->text().trimmed();

    if (inputUrl.isEmpty() || outputUrl.isEmpty()) {
        QMessageBox::warning(this, "Error", "URL不能为空！");
        ui->pushstreamButton->setChecked(false); // 恢复按钮状态
        return;
    }

    if (checked) {
        // 按钮被按下（开始推流）
        mPlayer->startPushing(inputUrl, outputUrl);
        ui->pushstreamButton->setText("停止推流");  // 更新按钮文字
    } else {
        // 按钮弹起（停止推流）
        mPlayer->stopPushing();
        ui->pushstreamButton->setText("开始推流");  // 恢复按钮文字
    }
}

void MainWindow::onPushButtonReset() {
    if (ui->pushstreamButton->isChecked()) {
        ui->pushstreamButton->setChecked(false); // 强制复位按钮
        ui->pushstreamButton->setText("开始推流");
        QMessageBox::warning(this, "Error", "推流异常终止！");
    }
}

