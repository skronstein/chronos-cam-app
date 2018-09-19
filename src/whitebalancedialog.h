#ifndef WHITEBALANCEDIALOG_H
#define WHITEBALANCEDIALOG_H

#include <QDialog>
#include "camera.h"
#include "statuswindow.h"

namespace Ui {
class whiteBalanceDialog;
}

class whiteBalanceDialog : public QDialog
{
	Q_OBJECT

public:
	explicit whiteBalanceDialog(QWidget *parent = 0, Camera * cameraInst = NULL);
	~whiteBalanceDialog();

private slots:
	void on_comboWB_currentIndexChanged(int index);

	void on_cmdSetCustomWB_clicked();

	void on_cmdClose_clicked();
	
	void on_cmdResetCustomWB_clicked();
	
	void on_spinGainFudgeFactor_valueChanged(double arg1);
	
private:
	Ui::whiteBalanceDialog *ui;
	Camera * camera;
	bool windowInitComplete;
	StatusWindow * sw;
	double sceneWhiteBalPresets[7][3];
	double customWhiteBalOld[3] = {1.0, 1.0, 1.0};
	void addPreset(double r, double b, double g, QString s);
	void addCustomPreset();
};

#endif // WHITEBALANCEDIALOG_H
