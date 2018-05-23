#include "whitebalancedialog.h"
#include "ui_whitebalancedialog.h"
#include <QMessageBox>
#include <QSettings>

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
	autoSetColorStuff = false;
	ui->comboWB->setCurrentIndex(0);
	autoSetColorStuff = true;
	camera->setCustomWhiteBal();
	QString str;
	str.append(QString::number(camera->sceneWhiteBalMatrix[0]));
	str.append(", ");
	str.append(QString::number(camera->sceneWhiteBalMatrix[1]));
	str.append(", ");
	str.append(QString::number(camera->sceneWhiteBalMatrix[2]));
	ui->label->setText(str);
}
