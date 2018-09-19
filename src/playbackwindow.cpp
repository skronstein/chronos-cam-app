/****************************************************************************
 *  Copyright (C) 2013-2017 Kron Technologies Inc <http://www.krontech.ca>. *
 *                                                                          *
 *  This program is free software: you can redistribute it and/or modify    *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ****************************************************************************/
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>

#include "util.h"
#include "camera.h"

#include "savesettingswindow.h"
#include "playbackwindow.h"
#include "ui_playbackwindow.h"

#include <QTimer>
#include <QMessageBox>
#include <QSettings>
#include <QKeyEvent>

#define MIN_FREE_SPACE 20000000

playbackWindow::playbackWindow(QWidget *parent, Camera * cameraInst, bool autosave) :
	QWidget(parent),
	ui(new Ui::playbackWindow)
{
	QSettings appSettings;
	VideoStatus vStatus;
	ui->setupUi(this);
	this->setWindowFlags(Qt::Dialog /*| Qt::WindowStaysOnTopHint*/ | Qt::FramelessWindowHint);

	camera = cameraInst;
	autoSaveFlag = autosave;
	autoRecordFlag = camera->get_autoRecord();
	this->move(camera->ButtonsOnLeft? 0:600, 0);
	saveAborted = false;
	
	camera->vinst->getStatus(&vStatus);
	playFrame = 0;
	totalFrames = vStatus.totalFrames;

	sw = new StatusWindow;

	connect(ui->cmdClose, SIGNAL(clicked()), this, SLOT(close()));

	ui->verticalSlider->setMinimum(0);
	ui->verticalSlider->setMaximum(totalFrames - 1);
	ui->verticalSlider->setValue(playFrame);
	ui->cmdLoop->setVisible(appSettings.value("camera/demoMode", false).toBool());
	markInFrame = 1;
	markOutFrame = totalFrames;
	ui->verticalSlider->setHighlightRegion(markInFrame, markOutFrame);

	camera->setPlayMode(true);
	camera->vinst->setPosition(0, 0);

	playbackExponent = 0;
	updatePlayRateLabel();

	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(updatePlayFrame()));
	timer->start(30);

	updateStatusText();

	settingsWindowIsOpen = false;

	if(autoSaveFlag) {
		on_cmdSave_clicked();
	}
	
	if(camera->vinst->getOverlayStatus())	camera->vinst->setOverlay("%.6h/%.6z Sg=%g/%i T=%.8Ss");
	
	ui->spinGainFudgeFactor->setValue(camera->GainFudgeFactor);
}

playbackWindow::~playbackWindow()
{
	qDebug()<<"playbackwindow deconstructor";
	camera->setPlayMode(false);
	timer->stop();
	emit finishedSaving();
	delete sw;
	delete ui;
}

void playbackWindow::on_verticalSlider_sliderMoved(int position)
{
	/* Note that a rate of zero will also pause playback. */
	camera->vinst->setPosition(position, 0);
}

void playbackWindow::on_verticalSlider_valueChanged(int value)
{

}

void playbackWindow::on_cmdPlayForward_pressed()
{
	int fps = (playbackExponent >= 0) ? (60 << playbackExponent) : 60 / (1 - playbackExponent);
	camera->vinst->setPlayback(fps);
}

void playbackWindow::on_cmdPlayForward_released()
{
	camera->vinst->setPlayback(0);
}

void playbackWindow::on_cmdPlayReverse_pressed()
{
	int fps = (playbackExponent >= 0) ? (60 << playbackExponent) : 60 / (1 - playbackExponent);
	camera->vinst->setPlayback(-fps);
}

void playbackWindow::on_cmdPlayReverse_released()
{
	camera->vinst->setPlayback(0);
}

void playbackWindow::on_cmdSave_clicked()
{
	UInt32 ret;
	QMessageBox msg;
	char parentPath[1000];
	struct statvfs statvfsBuf;
	struct statfs fileSystemInfoBuf;
	uint64_t estimatedSize;
	QSettings appSettings;
	
	autoRecordFlag = camera->autoRecord = camera->get_autoRecord();

	//Build the parent path of the save directory, to determine if it's a mount point
	strcpy(parentPath, camera->vinst->fileDirectory);
	strcat(parentPath, "/..");

	if(camera->vinst->getStatus(NULL) != VIDEO_STATE_FILESAVE)
	{
		//If no directory set, complain to the user
		if(strlen(camera->vinst->fileDirectory) == 0)
		{
			msg.setText("No save location set! Set save location in Settings");
			msg.exec();
			return;
		}

		if (!statvfs(camera->vinst->fileDirectory, &statvfsBuf)) {
			qDebug("===================================");
			
			// calculated estimated size
			estimatedSize = (markOutFrame - markInFrame + 3);// +3 instead of +1 because the length of a saved video can be up to 2 frames more than the length of the region selected.
			qDebug("Number of frames: %llu", estimatedSize);
			estimatedSize *= appSettings.value("camera/hRes", MAX_FRAME_SIZE_H).toInt();
			estimatedSize *= appSettings.value("camera/vRes", MAX_FRAME_SIZE_V).toInt();
			qDebug("Resolution: %d x %d", appSettings.value("camera/hRes", MAX_FRAME_SIZE_H).toInt(), appSettings.value("camera/vRes", MAX_FRAME_SIZE_V).toInt());
			// multiply by bits per pixel
			switch(getSaveFormat()) {
			case SAVE_MODE_H264:
				// the *1.2 part is fudge factor
				estimatedSize = (uint64_t) ((double)estimatedSize * appSettings.value("recorder/bitsPerPixel", camera->vinst->bitsPerPixel).toDouble() * 1.2);
				qDebug("Bits/pixel: %0.3f", appSettings.value("recorder/bitsPerPixel", camera->vinst->bitsPerPixel).toDouble());
				break;
			case SAVE_MODE_DNG:
			case SAVE_MODE_RAW16:
				qDebug("Bits/pixel: %d", 16);
				estimatedSize *= 16;
				estimatedSize += (4096<<8);
				break;
			case SAVE_MODE_RAW12:
				qDebug("Bits/pixel: %d", 12);
				estimatedSize *= 12;
				estimatedSize += (4096<<8);
				break;
			case SAVE_MODE_TIFF:
				estimatedSize *= 24;
				estimatedSize += (4096<<8);
				break;

			default:
				// unknown format
				qDebug("Bits/pixel: unknown - default: %d", 16);
				estimatedSize *= 16;
			}
			// convert to bytes
			estimatedSize /= 8;

			qDebug("Free space: %llu  (%lu * %lu)", statvfsBuf.f_bsize * (uint64_t)statvfsBuf.f_bfree, statvfsBuf.f_bsize, statvfsBuf.f_bfree);
			qDebug("Estimated file size: %llu", estimatedSize);
			
			qDebug("===================================");

			statfs(camera->vinst->fileDirectory, &fileSystemInfoBuf);
			bool fileOverMaxSize = (estimatedSize > 4294967296 && fileSystemInfoBuf.f_type == 0x4d44);//If file size is over 4GB and file system is FAT32
			insufficientFreeSpaceEstimate = (estimatedSize > (statvfsBuf.f_bsize * (uint64_t)statvfsBuf.f_bfree));
			
			//If amount of free space is below both 10MB and below the estimated size of the video, do not allow the save to start
			if(insufficientFreeSpaceEstimate && MIN_FREE_SPACE > (statvfsBuf.f_bsize * (uint64_t)statvfsBuf.f_bfree)){
				QMessageBox::warning(this, "Warning - Insufficient free space", "Cannot save a video because of insufficient free space", QMessageBox::Ok);
				return;
			}
			
			/*qDebug()<<"autoSaveFlag = " <<autoSaveFlag;
			qDebug()<<"fileOverMaxSize = " <<fileOverMaxSize;
			qDebug()<<"insufficientFreeSpaceEstimate = " <<insufficientFreeSpaceEstimate;*/
			
			if(!autoSaveFlag){
				if (fileOverMaxSize && !insufficientFreeSpaceEstimate) {
					QMessageBox::StandardButton reply;
					reply = QMessageBox::warning(this, "Warning - File size over limit", "Estimated file size is larger than the 4GB limit for the the filesystem.\nAttempt to save anyway?", QMessageBox::Yes|QMessageBox::No);
					if(QMessageBox::Yes != reply)
						return;
				}
				
				if (insufficientFreeSpaceEstimate && !fileOverMaxSize) {
					QMessageBox::StandardButton reply;
					reply = QMessageBox::warning(this, "Warning - Insufficient free space", "Estimated file size is larger than free space on drive.\nAttempt to save anyway?", QMessageBox::Yes|QMessageBox::No);
					if(QMessageBox::Yes != reply)
						return;
				}
				
				if (fileOverMaxSize && insufficientFreeSpaceEstimate){
					QMessageBox::StandardButton reply;
					reply = QMessageBox::warning(this, "Warning - File size over limits", "Estimated file size is larger than free space on drive.\nEstimated file size is larger than the 4GB limit for the the filesystem.\nAttempt to save anyway?", QMessageBox::Yes|QMessageBox::No);
					if(QMessageBox::Yes != reply)
						return;
				}
			}
		}

		//Check that the path exists
		struct stat sb;
		struct stat sbP;
		if (stat(camera->vinst->fileDirectory, &sb) == 0 && S_ISDIR(sb.st_mode) &&
				stat(parentPath, &sbP) == 0 && sb.st_dev != sbP.st_dev)		//If location is directory and is a mount point (device ID of parent is different from device ID of path)
		{
			ret = camera->startSave(markInFrame - 1, markOutFrame - markInFrame + 1);
			if(RECORD_FILE_EXISTS == ret)
			{
				if(camera->vinst->errorCallback)
					(*camera->vinst->errorCallback)(camera->vinst->errorCallbackArg, "file already exists");
				msg.setText("File already exists. Rename then try saving again.");
				msg.exec();
				return;
			}
			else if(RECORD_DIRECTORY_NOT_WRITABLE == ret)
			{
				if(camera->vinst->errorCallback)
					(*camera->vinst->errorCallback)(camera->vinst->errorCallbackArg, "save directory is not writable");
				msg.setText("Save directory is not writable.");
				msg.exec();
				return;
			}
			else if(RECORD_INSUFFICIENT_SPACE == ret)
			{
				if(camera->vinst->errorCallback) {
					(*camera->vinst->errorCallback)(camera->vinst->errorCallbackArg, "insufficient free space");
				}
				msg.setText("Selected device does not have sufficient free space.");
				msg.exec();
				return;
			}

			ui->cmdSave->setText("Abort\nSave");
			setControlEnable(false);
			sw->setText("Saving...");
			sw->show();

			saveDoneTimer = new QTimer(this);
			connect(saveDoneTimer, SIGNAL(timeout()), this, SLOT(checkForSaveDone()));
			saveDoneTimer->start(100);

			/* Prevent the user from pressing the abort/save button just after the last frame,
			 * as that can make the camera try to save a 2nd video too soon, crashing the camapp.
			 * It is also disabled in checkForSaveDone(), but if the video is very short,
			 * that might not be called at all before the end of the video, so just disable the button right away.*/
			if(markOutFrame - markInFrame < 25) ui->cmdSave->setEnabled(false);

			ui->verticalSlider->appendRegionToList();
			ui->verticalSlider->setHighlightRegion(markOutFrame, markOutFrame);
			//both arguments should be markout because a new rectangle will be drawn,
			//and it should not overlap the one that was just appended
			emit enableSaveSettingsButtons(false);
		}
		else
		{
			if(camera->vinst->errorCallback)
				(*camera->vinst->errorCallback)(camera->vinst->errorCallbackArg, "location not found");
			msg.setText(QString("Save location ") + QString(camera->vinst->fileDirectory) + " not found, set save location in Settings");
			msg.exec();
			return;
		}
	}
	else
	{
		//This block is executed when Abort is clicked
		//or when save is automatically aborted due to full storage
		camera->vinst->stopRecording();
		ui->verticalSlider->removeLastRegionFromList();
		ui->verticalSlider->setHighlightRegion(markInFrame, markOutFrame);
		saveAborted = true;
		autoSaveFlag = false;
		autoRecordFlag = false;
		//camera->autoRecord = false;
		sw->setText("Aborting...");
		//qDebug()<<"Aborting...";
	}

}

void playbackWindow::on_cmdStopSave_clicked()
{

}

void playbackWindow::on_cmdSaveSettings_clicked()
{
	saveSettingsWindow *w = new saveSettingsWindow(NULL, camera);
	w->setAttribute(Qt::WA_DeleteOnClose);
	w->show();
	
	settingsWindowIsOpen = true;
	if(camera->ButtonsOnLeft) w->move(230, 0);
	ui->cmdSaveSettings->setEnabled(false);
	ui->cmdClose->setEnabled(false);
	connect(w, SIGNAL(destroyed()), this, SLOT(saveSettingsClosed()));
	connect(this, SIGNAL(enableSaveSettingsButtons(bool)), w, SLOT(setControlEnable(bool)));
	connect(this, SIGNAL(destroyed(QObject*)), w, SLOT(close()));
}

void playbackWindow::saveSettingsClosed(){
	settingsWindowIsOpen = false;
	if(camera->vinst->getStatus(NULL) != VIDEO_STATE_FILESAVE) {
		/* Only enable these buttons if the camera is not saving a video */
		ui->cmdSaveSettings->setEnabled(true);
		ui->cmdClose->setEnabled(true);
	}
}

void playbackWindow::on_cmdMarkIn_clicked()
{
    markInFrame = playFrame + 1;
	if(markOutFrame < markInFrame)
		markOutFrame = markInFrame;
	ui->verticalSlider->setHighlightRegion(markInFrame, markOutFrame);
	updateStatusText();
}

void playbackWindow::on_cmdMarkOut_clicked()
{
    markOutFrame = playFrame + 1;
	if(markInFrame > markOutFrame)
		markInFrame = markOutFrame;
	ui->verticalSlider->setHighlightRegion(markInFrame, markOutFrame);
	updateStatusText();
}

void playbackWindow::keyPressEvent(QKeyEvent *ev)
{
	unsigned int skip = 1;
	unsigned int nextFrame;
	switch (ev->key()) {
	case Qt::Key_PageUp:
		skip = 10;
		if (playbackExponent > 0) {
			skip <<= playbackExponent;
		}
	case Qt::Key_Up:
		playFrame = (playFrame + skip) % totalFrames;
		camera->vinst->setPosition(playFrame, 0);
		break;

	case Qt::Key_PageDown:
		skip = 10;
		if (playbackExponent > 0) {
			skip <<= playbackExponent;
		}
	case Qt::Key_Down:
		if (playFrame >= skip) {
			playFrame = playFrame - skip;
		} else {
			playFrame = playFrame + totalFrames - skip;
		}
		camera->vinst->setPosition(playFrame, 0);
		break;
	}
}

void playbackWindow::updateStatusText()
{
	char text[100];
    sprintf(text, "Frame %d/%d\r\nMark start %d\r\nMark end %d", playFrame + 1, totalFrames, markInFrame, markOutFrame);
	ui->lblInfo->setText(text);
}

//Periodically check if the play frame is updated
void playbackWindow::updatePlayFrame()
{
    playFrame = camera->vinst->getPosition();
    ui->verticalSlider->setValue(playFrame);
	updateStatusText();
}

//Once save is done, re-enable the window
void playbackWindow::checkForSaveDone()
{
	VideoStatus st;
	if(camera->vinst->getStatus(&st) != VIDEO_STATE_FILESAVE)
	{
		saveDoneTimer->stop();
		delete saveDoneTimer;

		sw->close();
		ui->cmdSave->setText("Save");
		setControlEnable(true);
		emit enableSaveSettingsButtons(true);
		ui->cmdSave->setEnabled(true);
		saveAborted = false;
		updatePlayRateLabel();
		ui->verticalSlider->setHighlightRegion(markInFrame, markOutFrame);

		if(autoRecordFlag) {
			qDebug()<<".  closing";
			emit finishedSaving();
			delete this;
		}
	}
	else {
		char tmp[64];
		sprintf(tmp, "%.1ffps", st.framerate);
		ui->lblFrameRate->setText(tmp);
		setControlEnable(false);

		struct statvfs statvfsBuf;
		statvfs(camera->vinst->fileDirectory, &statvfsBuf);
		qDebug("Free space: %llu  (%lu * %lu)", statvfsBuf.f_bsize * (uint64_t)statvfsBuf.f_bfree, statvfsBuf.f_bsize, statvfsBuf.f_bfree);
		
		/* Prevent the user from pressing the abort/save button just after the last frame,
		 * as that can make the camera try to save a 2nd video too soon, crashing the camapp.*/
		if(playFrame >= markOutFrame - 25)
			ui->cmdSave->setEnabled(false);
		
		/*Abort the save if insufficient free space,
		but not if the save has already been aborted,
		or if the save button is not enabled(unsafe to abort at that time)(except if save mode is RAW)*/
		bool insufficientFreeSpaceCurrent = (MIN_FREE_SPACE > statvfsBuf.f_bsize * (uint64_t)statvfsBuf.f_bfree);
		if(insufficientFreeSpaceCurrent &&
		   insufficientFreeSpaceEstimate &&
		   !saveAborted &&
				(ui->cmdSave->isEnabled() ||
				getSaveFormat() != SAVE_MODE_H264)
		   ) {
			on_cmdSave_clicked();
			sw->setText("Storage is now full; Aborting...");			
		}
	}
}

void playbackWindow::on_cmdRateUp_clicked()
{
	if(playbackExponent < 5)
		playbackExponent++;

	updatePlayRateLabel();
}

void playbackWindow::on_cmdRateDn_clicked()
{
	if(playbackExponent > -5)
		playbackExponent--;

	updatePlayRateLabel();
}

void playbackWindow::updatePlayRateLabel(void)
{
	char playRateStr[100];
	double playRate;

	playRate = (playbackExponent >= 0) ? (60 << playbackExponent) : 60.0 / (1 - playbackExponent);
	sprintf(playRateStr, "%.1ffps", playRate);

	ui->lblFrameRate->setText(playRateStr);
}

void playbackWindow::setControlEnable(bool en)
{
	//While settings window is open, don't let the user
	//close the playback window or open another settings window.
	if(!settingsWindowIsOpen){
		ui->cmdClose->setEnabled(en);
		ui->cmdSaveSettings->setEnabled(en);
	}
	ui->cmdMarkIn->setEnabled(en);
	ui->cmdMarkOut->setEnabled(en);
	ui->cmdPlayForward->setEnabled(en);
	ui->cmdPlayReverse->setEnabled(en);
	ui->cmdRateDn->setEnabled(en);
	ui->cmdRateUp->setEnabled(en);
	ui->verticalSlider->setEnabled(en);
}

void playbackWindow::on_cmdClose_clicked()
{
    camera->videoHasBeenReviewed = true;
    camera->autoRecord = false;
}

UInt32 playbackWindow::getSaveFormat(){
	QSettings appSettings;
	return appSettings.value("recorder/saveFormat", 0).toUInt();
}

void playbackWindow::on_cmdLoop_clicked()
{
	int fps = (playbackExponent >= 0) ? (60 << playbackExponent) : 60 / (1 - playbackExponent);
	unsigned int count = (markOutFrame - markInFrame + 1);
	camera->vinst->loopPlayback(markInFrame, count, fps);
}

void playbackWindow::on_spinGainFudgeFactor_valueChanged(double arg1)
{
	camera->GainFudgeFactor = arg1;
	camera->setCCMatrix();
}
