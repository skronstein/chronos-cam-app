#include "whitebalancedialog.h"
#include "ui_whitebalancedialog.h"
#include <QMessageBox>
#include <QSettings>
#include "util.h"

whiteBalanceDialog::whiteBalanceDialog(QWidget *parent, Camera * cameraInst) :
	QDialog(parent),
	ui(new Ui::whiteBalanceDialog)
{
	autoSetColorStuff = false;
	ui->setupUi(this);
	camera = cameraInst;
	this->setWindowFlags(Qt::Dialog /*| Qt::WindowStaysOnTopHint*/ | Qt::FramelessWindowHint);
	this->move(camera->ButtonsOnLeft? 0:600, 0);
	connect(ui->cmdClose, SIGNAL(clicked(bool)), this, SLOT(close()));
	sw = new StatusWindow;
	ui->comboWB->setCurrentIndex(camera->getWBIndex());
	autoSetColorStuff = true;
}

whiteBalanceDialog::~whiteBalanceDialog()
{
	delete ui;
}

void whiteBalanceDialog::on_comboWB_currentIndexChanged(int index)
{
	if(!autoSetColorStuff) return;
	camera->setWBIndex(index);
	camera->setSceneWhiteBalMatrix();
	camera->setCCMatrix();
}

void whiteBalanceDialog::on_cmdSetCustomWB_clicked()
{
	QMessageBox::StandardButton reply;
	reply = QMessageBox::question(this, "Set white balance?", "Will set white balance. Continue?", QMessageBox::Yes|QMessageBox::No);
	if(QMessageBox::Yes != reply)
		return;

	double Rsum = 0, Gsum = 0, Bsum = 0;
	int itr;
	for(itr = 0; itr < 1; itr++){
		Int32 ret = camera->setWhiteBalance(camera->getImagerSettings().hRes / 2 & 0xFFFFFFFE,
									 camera->getImagerSettings().vRes / 2 & 0xFFFFFFFE);	//Sample from middle but make sure position is a multiple of 2
		if(ret == CAMERA_CLIPPED_ERROR)
		{
			sw->setText("Clipping. Reduce exposure and try white balance again");
			sw->setTimeout(3000);
			sw->show();
			return;
		}
		else if(ret == CAMERA_LOW_SIGNAL_ERROR)
		{
			sw->setText("Too dark. Increase exposure and try white balance again");
			sw->setTimeout(3000);
			sw->show();
			return;
		}
		Rsum += camera->sceneWhiteBalMatrix[0];
		Gsum += camera->sceneWhiteBalMatrix[1];
		Bsum += camera->sceneWhiteBalMatrix[2];
		delayms(17);
	}
	autoSetColorStuff = false;
	ui->comboWB->setCurrentIndex(0);
	autoSetColorStuff = true;
	camera->setCustomWhiteBal();
	QString str;
	str.append("R = ");
	str.append(QString::number(Rsum/(double)itr));
	str.append("\nG = ");
	str.append(QString::number(Gsum/(double)itr));
	str.append("\nB = ");
	str.append(QString::number(Bsum/(double)itr));
	ui->label->setText(str);
}

void whiteBalanceDialog::on_cmdSetCustomWB_2_clicked()
{
	QMessageBox::StandardButton reply;
	reply = QMessageBox::question(this, "Set white balance?", "Will set white balance. Continue?", QMessageBox::Yes|QMessageBox::No);
	if(QMessageBox::Yes != reply)
		return;

	double Rsum = 0, Gsum = 0, Bsum = 0;
	int itr;
	for(itr = 0; itr < 200; itr++){
		Int32 ret = camera->setWhiteBalance(camera->getImagerSettings().hRes / 2 & 0xFFFFFFFE,
									 camera->getImagerSettings().vRes / 2 & 0xFFFFFFFE);	//Sample from middle but make sure position is a multiple of 2
		if(ret == CAMERA_CLIPPED_ERROR)
		{
			sw->setText("Clipping. Reduce exposure and try white balance again");
			sw->setTimeout(3000);
			sw->show();
			return;
		}
		else if(ret == CAMERA_LOW_SIGNAL_ERROR)
		{
			sw->setText("Too dark. Increase exposure and try white balance again");
			sw->setTimeout(3000);
			sw->show();
			return;
		}
		Rsum += camera->sceneWhiteBalMatrix[0];
		Gsum += camera->sceneWhiteBalMatrix[1];
		Bsum += camera->sceneWhiteBalMatrix[2];
		delayms(17);
	}
	autoSetColorStuff = false;
	ui->comboWB->setCurrentIndex(0);
	autoSetColorStuff = true;
	QString str;
	str.append("R = ");
	str.append(QString::number(Rsum/(double)itr));
	str.append("\nG = ");
	str.append(QString::number(Gsum/(double)itr));
	str.append("\nB = ");
	str.append(QString::number(Bsum/(double)itr));
	ui->label->setText(str);
	camera->sceneWhiteBalMatrix[0] = Rsum/(double)itr;
	camera->sceneWhiteBalMatrix[1] = Gsum/(double)itr;
	camera->sceneWhiteBalMatrix[2] = Bsum/(double)itr;
	camera->setCustomWhiteBal();
}
