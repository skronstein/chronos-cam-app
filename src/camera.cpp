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
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <QDebug>
#include <semaphore.h>
#include <QSettings>
#include <QResource>
#include <QDir>
#include <QScreen>
#include <QIODevice>
#include <QApplication>

#include "font.h"
#include "camera.h"
#include "gpmc.h"
#include "gpmcRegs.h"
#include "cameraRegisters.h"
#include "util.h"
#include "types.h"
#include "lux1310.h"
#include "sensor.h"
#include "defines.h"
#include <QWSDisplay>

#define USE_3POINT_CAL 0

void* recDataThread(void *arg);


Camera::Camera()
{
	QSettings appSettings;

	terminateRecDataThread = false;
	lastRecording = false;
	playbackMode = false;
	recording = false;
	imgGain = 1.0;
	recordingData.ignoreSegments = 0;
	recordingData.hasBeenSaved = true;
	recordingData.hasBeenViewed = true;
	unsavedWarnEnabled = getUnsavedWarnEnable();
	autoSave = appSettings.value("camera/autoSave", 0).toBool();
	autoRecord = appSettings.value("camera/autoRecord", 0).toBool();
	ButtonsOnLeft = getButtonsOnLeft();
	UpsideDownDisplay = getUpsideDownDisplay();
	strcpy(serialNumber, "Not_Set");

	pinst = new Power();
}

Camera::~Camera()
{
	terminateRecDataThread = true;
	pthread_join(recDataThreadID, NULL);

	delete pinst;
}

CameraErrortype Camera::init(GPMC * gpmcInst, Video * vinstInst, ImageSensor * sensorInst, UserInterface * userInterface, UInt32 ramSizeVal, bool color)
{
	CameraErrortype retVal;
	UInt32 ramSizeGBSlot0, ramSizeGBSlot1;
	QSettings appSettings;

	//Get the memory size
	retVal = (CameraErrortype)getRamSizeGB(&ramSizeGBSlot0, &ramSizeGBSlot1);

	if(retVal != SUCCESS)
		return retVal;

	//Read serial number in
    retVal = (CameraErrortype)readSerialNumber(serialNumber);
	if(retVal != SUCCESS)
		return retVal;

	gpmc = gpmcInst;
	vinst = vinstInst;
	sensor = sensorInst;
	ui = userInterface;
	ramSize = (ramSizeGBSlot0 + ramSizeGBSlot1)*1024/32*1024*1024;
	int err;

	/* Color detection or override from env. */
	const char *envColor = getenv("CAM_OVERRIDE_COLOR");
	if (envColor) {
		char *endp;
		unsigned long uval = strtoul(envColor, &endp, 0);
		if (*endp == '\0') isColor = (uval != 0);
		else if (strcasecmp(envColor, "COLOR") == 0) isColor = true;
		else if (strcasecmp(envColor, "TRUE") == 0) isColor = true;
		else if (strcasecmp(envColor, "MONO") == 0) isColor = false;
		else if (strcasecmp(envColor, "FALSE") == 0) isColor = false;
		else isColor = readIsColor();
	}
	else {
		isColor = readIsColor();
	}

	//dummy read
	if(getRecording())
		qDebug("rec true at init");
	int v = 0;

	if(1)
	{

		//Reset FPGA
		gpmc->write16(SYSTEM_RESET_ADDR, 1);
		v++;
		//Give the FPGA some time to reset
		delayms(200);
	}

	if(ACCEPTABLE_FPGA_VERSION != getFPGAVersion())
	{
		return CAMERA_WRONG_FPGA_VERSION;
	}

	gpmc->write16(IMAGE_SENSOR_FIFO_START_ADDR, 0x0100);
	gpmc->write16(IMAGE_SENSOR_FIFO_STOP_ADDR, 0x0100);

	gpmc->write32(SEQ_LIVE_ADDR_0_ADDR, LIVE_REGION_START + MAX_FRAME_WORDS*0);
	gpmc->write32(SEQ_LIVE_ADDR_1_ADDR, LIVE_REGION_START + MAX_FRAME_WORDS*1);
	gpmc->write32(SEQ_LIVE_ADDR_2_ADDR, LIVE_REGION_START + MAX_FRAME_WORDS*2);

	if (ramSizeGBSlot1 != 0) {
	if      (ramSizeGBSlot0 == 0)                         { gpmc->write16(MMU_CONFIG_ADDR, MMU_INVERT_CS);      qDebug("--- memory --- invert CS remap"); }
	else if (ramSizeGBSlot0 == 8 && ramSizeGBSlot1 == 16) { gpmc->write16(MMU_CONFIG_ADDR, MMU_INVERT_CS);		qDebug("--- memory --- invert CS remap"); }
		else if (ramSizeGBSlot0 == 8 && ramSizeGBSlot1 == 8)  { gpmc->write16(MMU_CONFIG_ADDR, MMU_SWITCH_STUFFED); qDebug("--- memory --- switch stuffed remap"); }
		else {
			qDebug("--- memory --- no remap");
		}
	}
	else {
		qDebug("--- memory --- no remap");
	}
	qDebug("--- memory --- remap register: 0x%04X", gpmc->read32(MMU_CONFIG_ADDR));


	//enable video readout
	gpmc->write32(DISPLAY_CTL_ADDR, (gpmc->read32(DISPLAY_CTL_ADDR) & ~DISPLAY_CTL_READOUT_INH_MASK) | (isColor ? DISPLAY_CTL_COLOR_MODE_MASK : 0));

	printf("Starting rec data thread\n");
	terminateRecDataThread = false;

	err = pthread_create(&recDataThreadID, NULL, &recDataThread, this);
	if(err)
		return CAMERA_THREAD_ERROR;


	retVal = sensor->init(gpmc);
//mem problem before this
	if(retVal != SUCCESS)
	{
		return retVal;
	}

	io = new IO(gpmc);
	retVal = io->init();
	if(retVal != SUCCESS)
		return retVal;

	/* Load default recording from sensor limits. */
	imagerSettings.geometry = sensor->getMaxGeometry();
	imagerSettings.geometry.vDarkRows = 0;
	imagerSettings.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&imagerSettings.geometry);
	imagerSettings.period = sensor->getMinFramePeriod(&imagerSettings.geometry);
	imagerSettings.exposure = sensor->getMaxIntegrationTime(imagerSettings.period, &imagerSettings.geometry);
	imagerSettings.disableRingBuffer = 0;
	imagerSettings.mode = RECORD_MODE_NORMAL;
	imagerSettings.prerecordFrames = 1;
	imagerSettings.segmentLengthFrames = imagerSettings.recRegionSizeFrames;
	imagerSettings.segments = 1;

	//Set to full resolution
	ImagerSettings_t settings;

	settings.geometry.hRes          = appSettings.value("camera/hRes", imagerSettings.geometry.hRes).toInt();
	settings.geometry.vRes          = appSettings.value("camera/vRes", imagerSettings.geometry.vRes).toInt();
	settings.geometry.hOffset       = appSettings.value("camera/hOffset", 0).toInt();
	settings.geometry.vOffset       = appSettings.value("camera/vOffset", 0).toInt();
	settings.geometry.vDarkRows     = 0;
	settings.geometry.bitDepth		= imagerSettings.geometry.bitDepth;
	settings.gain                   = appSettings.value("camera/gain", 1).toInt();
	settings.period                 = appSettings.value("camera/period", sensor->getMinFramePeriod(&settings.geometry)).toInt();
	settings.exposure               = appSettings.value("camera/exposure", sensor->getMaxIntegrationTime(settings.period, &settings.geometry)).toInt();
	settings.recRegionSizeFrames    = appSettings.value("camera/recRegionSizeFrames", getMaxRecordRegionSizeFrames(&settings.geometry)).toInt();
	settings.disableRingBuffer      = appSettings.value("camera/disableRingBuffer", 0).toInt();
	settings.mode                   = (CameraRecordModeType)appSettings.value("camera/mode", RECORD_MODE_NORMAL).toInt();
	settings.prerecordFrames        = appSettings.value("camera/prerecordFrames", 1).toInt();
	settings.segmentLengthFrames    = appSettings.value("camera/segmentLengthFrames", settings.recRegionSizeFrames).toInt();
	settings.segments               = appSettings.value("camera/segments", 1).toInt();
	settings.temporary              = 0;

	setImagerSettings(settings);

	io->setTriggerDelayFrames(0, FLAG_USESAVED);
	setTriggerDelayValues((double) io->getTriggerDelayFrames() / settings.recRegionSizeFrames,
				 io->getTriggerDelayFrames() * ((double)settings.period / 100000000),
				 io->getTriggerDelayFrames());

	vinst->bitsPerPixel        = appSettings.value("recorder/bitsPerPixel", 0.7).toDouble();
	vinst->maxBitrate          = appSettings.value("recorder/maxBitrate", 40.0).toDouble();
	vinst->framerate           = appSettings.value("recorder/framerate", 60).toUInt();
	strcpy(vinst->filename,      appSettings.value("recorder/filename", "").toString().toAscii());
	strcpy(vinst->fileDirectory, appSettings.value("recorder/fileDirectory", "").toString().toAscii());
	if(strlen(vinst->fileDirectory) == 0){
		/* Set the default file path, or fall back to the MMC card. */
		int i;
		bool fileDirFoundOnUSB = false;
		for (i = 1; i <= 3; i++) {
			sprintf(vinst->fileDirectory, "/media/sda%d", i);
			if (path_is_mounted(vinst->fileDirectory)) {
				fileDirFoundOnUSB = true;
				break;
			}
		}
		if(!fileDirFoundOnUSB) strcpy(vinst->fileDirectory, "/media/mmcblk1p1");
	}

	maxPostFramesRatio = 1;

	/* Load calibration and perform perform automated cal. */
	sensor->loadADCOffsetsFromFile(&settings.geometry);
	loadColGainFromFile();

	if(CAMERA_FILE_NOT_FOUND == loadFPNFromFile()) {
		fastFPNCorrection();
	}

	/* Load color matrix from settings */
	if (isColor) {
		/* White Balance. */
		whiteBalMatrix[0] = appSettings.value("whiteBalance/currentR", 1.35).toDouble();
		whiteBalMatrix[1] = appSettings.value("whiteBalance/currentG", 1.00).toDouble();
		whiteBalMatrix[2] = appSettings.value("whiteBalance/currentB", 1.584).toDouble();

		/* Color Matrix */
		loadCCMFromSettings();
	}
	setCCMatrix(colorCalMatrix);
	setWhiteBalance(whiteBalMatrix);

	vinst->setDisplayOptions(getZebraEnable(), getFocusPeakEnable() ? (FocusPeakColors)getFocusPeakColor() : FOCUS_PEAK_DISABLE);
	vinst->setDisplayPosition(ButtonsOnLeft ^ UpsideDownDisplay);
	vinst->liveDisplay((sensor->getSensorQuirks() & SENSOR_QUIRK_UPSIDE_DOWN) != 0);
	setFocusPeakThresholdLL(appSettings.value("camera/focusPeakThreshold", 25).toUInt());

	printf("Video init done\n");
	return SUCCESS;
}

UInt32 Camera::setImagerSettings(ImagerSettings_t settings)
{
	QSettings appSettings;

	if(!sensor->isValidResolution(&settings.geometry) ||
		settings.recRegionSizeFrames < RECORD_LENGTH_MIN ||
		settings.segments > settings.recRegionSizeFrames) {
		return CAMERA_INVALID_IMAGER_SETTINGS;
	}

	sensor->seqOnOff(false);
	delayms(10);
	qDebug() << "Settings.period is" << settings.period;
	qDebug() << "Settings.exposure is" << settings.exposure;

	sensor->setResolution(&settings.geometry);
	sensor->setGain(settings.gain);
	sensor->setFramePeriod(settings.period, &settings.geometry);
	delayms(10);
	sensor->setIntegrationTime(settings.exposure, &settings.geometry);

	memcpy(&imagerSettings, &settings, sizeof(settings));

	//Zero trigger delay for Gated Burst
	if(settings.mode == RECORD_MODE_GATED_BURST) {
		io->setTriggerDelayFrames(0, FLAG_TEMPORARY);
	}

	UInt32 maxRecRegionSize = getMaxRecordRegionSizeFrames(&imagerSettings.geometry);
	if(settings.recRegionSizeFrames > maxRecRegionSize) {
		imagerSettings.recRegionSizeFrames = maxRecRegionSize;
	}
	else {
		imagerSettings.recRegionSizeFrames = settings.recRegionSizeFrames;
	}
	setRecRegion(REC_REGION_START, imagerSettings.recRegionSizeFrames, &imagerSettings.geometry);

	/* Load calibration. */
	sensor->loadADCOffsetsFromFile(&imagerSettings.geometry);
	loadColGainFromFile();

	qDebug()	<< "\nSet imager settings:\nhRes" << imagerSettings.geometry.hRes
				<< "vRes" << imagerSettings.geometry.vRes
				<< "vDark" << imagerSettings.geometry.vDarkRows
				<< "hOffset" << imagerSettings.geometry.hOffset
				<< "vOffset" << imagerSettings.geometry.vOffset
				<< "exposure" << imagerSettings.exposure
				<< "period" << imagerSettings.period
				<< "frameSizeWords" << getFrameSizeWords(&imagerSettings.geometry)
				<< "recRegionSizeFrames" << imagerSettings.recRegionSizeFrames;

	if (settings.temporary) {
		qDebug() << "--- settings --- temporary, not saving";
	}
	else {
		qDebug() << "--- settings --- saving";
		appSettings.setValue("camera/hRes",                 imagerSettings.geometry.hRes);
		appSettings.setValue("camera/vRes",                 imagerSettings.geometry.vRes);
		appSettings.setValue("camera/hOffset",              imagerSettings.geometry.hOffset);
		appSettings.setValue("camera/vOffset",              imagerSettings.geometry.vOffset);
		appSettings.setValue("camera/gain",                 imagerSettings.gain);
		appSettings.setValue("camera/period",               imagerSettings.period);
		appSettings.setValue("camera/exposure",             imagerSettings.exposure);
		appSettings.setValue("camera/recRegionSizeFrames",  imagerSettings.recRegionSizeFrames);
		appSettings.setValue("camera/disableRingBuffer",    imagerSettings.disableRingBuffer);
		appSettings.setValue("camera/mode",                 imagerSettings.mode);
		appSettings.setValue("camera/prerecordFrames",      imagerSettings.prerecordFrames);
		appSettings.setValue("camera/segmentLengthFrames",  imagerSettings.segmentLengthFrames);
		appSettings.setValue("camera/segments",             imagerSettings.segments);
	}

	return SUCCESS;
}

UInt32 Camera::getRecordLengthFrames(ImagerSettings_t settings)
{
	if ((settings.mode == RECORD_MODE_NORMAL) || (settings.mode == RECORD_MODE_GATED_BURST)) {
		return settings.recRegionSizeFrames;
	}
	else {
		return (settings.recRegionSizeFrames / settings.segments);
	}
}

UInt32 Camera::getFrameSizeWords(FrameGeometry *geometry)
{
	return ROUND_UP_MULT((geometry->size() + BYTES_PER_WORD - 1) / BYTES_PER_WORD, FRAME_ALIGN_WORDS);
}

UInt32 Camera::getMaxRecordRegionSizeFrames(FrameGeometry *geometry)
{
	return (ramSize - REC_REGION_START) / getFrameSizeWords(geometry);
}

void Camera::updateTriggerValues(ImagerSettings_t settings){
	UInt32 recLengthFrames = getRecordLengthFrames(settings);
	if(getTriggerDelayConstant() == TRIGGERDELAY_TIME_RATIO){
		triggerPostFrames  = triggerTimeRatio * recLengthFrames;
		triggerPostSeconds = triggerPostFrames * ((double)settings.period / 100000000);
	}
	if(getTriggerDelayConstant() == TRIGGERDELAY_SECONDS){
		triggerTimeRatio  = recLengthFrames / ((double)settings.period / 100000000);
		triggerPostFrames = triggerPostSeconds / ((double)settings.period / 100000000);
	}
	if(getTriggerDelayConstant() == TRIGGERDELAY_FRAMES){
		triggerTimeRatio   = (double)triggerPostFrames / recLengthFrames;
		triggerPostSeconds = triggerPostFrames * ((double)settings.period / 100000000);
	}
	io->setTriggerDelayFrames(triggerPostFrames);
}

unsigned short Camera::getTriggerDelayConstant(){
	 QSettings appSettings;
	//return appSettings.value("camera/triggerDelayConstant", TRIGGERDELAY_PRERECORDSECONDS).toUInt();
	return TRIGGERDELAY_TIME_RATIO;//With comboBox removed, always use this choice instead.
}

void Camera::setTriggerDelayConstant(unsigned short value){
	 QSettings appSettings;
	 appSettings.setValue("camera/triggerDelayConstant", value);
}

void Camera::setTriggerDelayValues(double ratio, double seconds, UInt32 frames){
	triggerTimeRatio = ratio;
	 triggerPostSeconds = seconds;
	 triggerPostFrames = frames;
}

UInt32 Camera::setIntegrationTime(double intTime, FrameGeometry *fSize, Int32 flags)
{
	QSettings appSettings;
	UInt32 validTime;
	UInt32 defaultTime = sensor->getMaxIntegrationTime(sensor->getFramePeriod(), fSize);
	if (flags & SETTING_FLAG_USESAVED) {
		validTime = appSettings.value("camera/exposure", defaultTime).toInt();
		qDebug("--- Using old settings --- Exposure time: %d (default: %d)", validTime, defaultTime);
		validTime = sensor->setIntegrationTime(validTime, fSize);
	}
	else {
		validTime = sensor->setIntegrationTime(intTime * sensor->getIntegrationClock(), fSize);
	}

	if (!(flags & SETTING_FLAG_TEMPORARY)) {
		qDebug("--- Saving settings --- Exposure time: %d", validTime);
		appSettings.setValue("camera/exposure", validTime);
		imagerSettings.exposure = validTime;
	}
	return SUCCESS;
}

void Camera::updateVideoPosition()
{
	vinst->setDisplayPosition(ButtonsOnLeft ^ UpsideDownDisplay);
}


Int32 Camera::startRecording(void)
{
	if(recording)
		return CAMERA_ALREADY_RECORDING;
	if(playbackMode)
		return CAMERA_IN_PLAYBACK_MODE;

	recordingData.valid = false;
	recordingData.hasBeenSaved = false;
	recordingData.hasBeenViewed = false;
	recordingData.ignoreSegments = 0;

	switch(imagerSettings.mode)
	{
	case RECORD_MODE_NORMAL:
	case RECORD_MODE_SEGMENTED:
		setRecSequencerModeNormal();
	break;

	case RECORD_MODE_GATED_BURST:
		setRecSequencerModeGatedBurst(imagerSettings.prerecordFrames);
	break;

	case RECORD_MODE_FPN:
		recordingData.ignoreSegments = 1;
	break;

	}

	vinst->flushRegions();
	startSequencer();
	ui->setRecLEDFront(true);
	ui->setRecLEDBack(true);
	recording = true;

	return SUCCESS;
}

Int32 Camera::setRecSequencerModeNormal()
{
	SeqPgmMemWord pgmWord;

	if(recording)
		return CAMERA_ALREADY_RECORDING;
	if(playbackMode)
		return CAMERA_IN_PLAYBACK_MODE;

	setRecRegion(REC_REGION_START, imagerSettings.recRegionSizeFrames, &imagerSettings.geometry);

	pgmWord.settings.termRecTrig = 0;
	pgmWord.settings.termRecMem = imagerSettings.disableRingBuffer ? 1 : 0;     //This currently doesn't work, bug in record sequencer hardware
	pgmWord.settings.termRecBlkEnd = (RECORD_MODE_SEGMENTED == imagerSettings.mode && imagerSettings.segments > 1) ? 0 : 1;
	pgmWord.settings.termBlkFull = 0;
	pgmWord.settings.termBlkLow = 0;
	pgmWord.settings.termBlkHigh = 0;
	pgmWord.settings.termBlkFalling = 0;
	pgmWord.settings.termBlkRising = 1;
	pgmWord.settings.next = 0;
	pgmWord.settings.blkSize = (imagerSettings.mode == RECORD_MODE_NORMAL ?
					imagerSettings.recRegionSizeFrames :
					imagerSettings.recRegionSizeFrames / imagerSettings.segments) - 1; //Set to number of frames desired minus one
	pgmWord.settings.pad = 0;

	qDebug() << "Setting record sequencer mode to" << (imagerSettings.mode == RECORD_MODE_NORMAL ? "normal" : "segmented") << ", disableRingBuffer =" << imagerSettings.disableRingBuffer << "segments ="
		 << imagerSettings.segments << "blkSize =" << pgmWord.settings.blkSize;
	writeSeqPgmMem(pgmWord, 0);

	return SUCCESS;
}

Int32 Camera::setRecSequencerModeGatedBurst(UInt32 prerecord)
{
	SeqPgmMemWord pgmWord;

	if(recording)
		return CAMERA_ALREADY_RECORDING;
	if(playbackMode)
		return CAMERA_IN_PLAYBACK_MODE;

	//Set to one plus the last valid address in the record region
	setRecRegion(REC_REGION_START, imagerSettings.recRegionSizeFrames, &imagerSettings.geometry);

	//Two instruction program
	//Instruction 0 records to a single frame while trigger is inactive
	//Instruction 1 records as normal while trigger is active

	//When trigger is inactive, we sit in this 1-frame block, continuously overwriting that frame
	pgmWord.settings.termRecTrig = 0;
	pgmWord.settings.termRecMem = 0;
	pgmWord.settings.termRecBlkEnd = 0;
	pgmWord.settings.termBlkFull = 0;
	pgmWord.settings.termBlkLow = 0;
	pgmWord.settings.termBlkHigh = 1;       //Terminate when trigger becomes active
	pgmWord.settings.termBlkFalling = 0;
	pgmWord.settings.termBlkRising = 0;
	pgmWord.settings.next = 1;              //Go to next block when this one terminates
	pgmWord.settings.blkSize = prerecord - 1;           //Set to number of frames desired minus one
	pgmWord.settings.pad = 0;

	writeSeqPgmMem(pgmWord, 0);

	pgmWord.settings.termRecTrig = 0;
	pgmWord.settings.termRecMem = imagerSettings.disableRingBuffer ? 1 : 0;;
	pgmWord.settings.termRecBlkEnd = 0;
	pgmWord.settings.termBlkFull = 0;
	pgmWord.settings.termBlkLow = 1;       //Terminate when trigger becomes inactive
	pgmWord.settings.termBlkHigh = 0;
	pgmWord.settings.termBlkFalling = 0;
	pgmWord.settings.termBlkRising = 0;
	pgmWord.settings.next = 0;              //Go back to block 0
	pgmWord.settings.blkSize = imagerSettings.recRegionSizeFrames-3; //Set to number of frames desired minus one
	pgmWord.settings.pad = 0;

	qDebug() << "---- Sequencer ---- Set to Gated burst mode, second block size:" << pgmWord.settings.blkSize+1;

	writeSeqPgmMem(pgmWord, 1);

	return SUCCESS;
}

Int32 Camera::setRecSequencerModeSingleBlock(UInt32 blockLength, UInt32 frameOffset)
{
	SeqPgmMemWord pgmWord;

	if(recording)
		return CAMERA_ALREADY_RECORDING;
	if(playbackMode)
		return CAMERA_IN_PLAYBACK_MODE;

	if((blockLength + frameOffset) > imagerSettings.recRegionSizeFrames)
		blockLength = imagerSettings.recRegionSizeFrames - frameOffset;

	//Set to one plus the last valid address in the record region
	setRecRegion(REC_REGION_START, imagerSettings.recRegionSizeFrames + frameOffset, &imagerSettings.geometry);

	pgmWord.settings.termRecTrig = 0;
	pgmWord.settings.termRecMem = 0;
	pgmWord.settings.termRecBlkEnd = 1;
	pgmWord.settings.termBlkFull = 1;
	pgmWord.settings.termBlkLow = 0;
	pgmWord.settings.termBlkHigh = 0;
	pgmWord.settings.termBlkFalling = 0;
	pgmWord.settings.termBlkRising = 0;
	pgmWord.settings.next = 0;
	pgmWord.settings.blkSize = blockLength-1; //Set to number of frames desired minus one
	pgmWord.settings.pad = 0;

	writeSeqPgmMem(pgmWord, 0);

	return SUCCESS;
}

Int32 Camera::setRecSequencerModeCalLoop(void)
{
	SeqPgmMemWord pgmWord;

	if(recording)
		return CAMERA_ALREADY_RECORDING;
	if(playbackMode)
		return CAMERA_IN_PLAYBACK_MODE;

	//Set to one plus the last valid address in the record region
	setRecRegion(CAL_REGION_START, CAL_REGION_FRAMES, &imagerSettings.geometry);

	pgmWord.settings.termRecTrig = 0;
	pgmWord.settings.termRecMem = 0;
	pgmWord.settings.termRecBlkEnd = 1;
	pgmWord.settings.termBlkFull = 0;
	pgmWord.settings.termBlkLow = 0;
	pgmWord.settings.termBlkHigh = 0;
	pgmWord.settings.termBlkFalling = 0;
	pgmWord.settings.termBlkRising = 0;
	pgmWord.settings.next = 0;
	pgmWord.settings.blkSize = CAL_REGION_FRAMES - 1;
	pgmWord.settings.pad = 0;

	writeSeqPgmMem(pgmWord, 0);
	return SUCCESS;
}

Int32 Camera::stopRecording(void)
{
	if(!recording)
		return CAMERA_NOT_RECORDING;

	terminateRecord();
	//recording = false;

	return SUCCESS;
}

bool Camera::getIsRecording(void)
{
	return recording;
}

UInt32 Camera::setPlayMode(bool playMode)
{
	if(recording)
		return CAMERA_ALREADY_RECORDING;
	if(!recordingData.valid)
		return CAMERA_NO_RECORDING_PRESENT;

	playbackMode = playMode;

	if(playMode)
	{
		vinst->setPosition(0);
	}
	else
	{
		bool videoFlip = (sensor->getSensorQuirks() & SENSOR_QUIRK_UPSIDE_DOWN) != 0;
		vinst->liveDisplay(videoFlip);
	}
	return SUCCESS;
}

/* Camera::readPixelCal
 *
 * Reads a 12-bit pixel out of acquisition RAM with calibration applied.
 *
 * x:		Horizontal pixel offset into the frame.
 * y:		Vertical pixel offset into the frame.
 * wordAddr: Memory address of the frame.
 * size:	Frame geometry.
 *
 * returns: Calibrated pixel value
 **/
UInt16 Camera::readPixelCal(UInt32 x, UInt32 y, UInt32 wordAddr, FrameGeometry *geometry)
{
	Int32 pixel = gpmc->readPixel12(y * geometry->hRes + x, wordAddr * BYTES_PER_WORD);
	UInt32 pxGain = (pixel * gpmc->read16(COL_GAIN_MEM_START_ADDR + (2 * x))) >> COL_GAIN_FRAC_BITS;

	/* Apply column curvature and offset terms for 3-point cal. */
	if (gpmc->read16(DISPLAY_GAIN_CONTROL_ADDR) & DISPLAY_GAIN_CONTROL_3POINT) {
		Int32 pxCurve = (pixel * pixel * (Int16)gpmc->read16(COL_CURVE_MEM_START_ADDR + (2 * x))) >> COL_CURVE_FRAC_BITS;
		Int32 pxOffset = (Int16)gpmc->read16(COL_OFFSET_MEM_START_ADDR + (2 * x));
		/* TODO: FPN is a bit messy in 3-point world (signed 12-bit). */
		return pxGain + pxCurve + pxOffset;
	}
	/* Otherwise - 2-point calibration requires FPN subtraction. */
	else {
		UInt32 fpn = gpmc->readPixel12(y * geometry->hRes + x, FPN_ADDRESS * BYTES_PER_WORD);
		return pxGain - fpn;
	}
}

void Camera::loadFPNCorrection(FrameGeometry *geometry, const UInt16 *fpnBuffer, UInt32 framesToAverage)
{
	UInt32 *fpnColumns = (UInt32 *)calloc(geometry->hRes, sizeof(UInt32));
	UInt8  *pixBuffer = (UInt8  *)calloc(1, geometry->size());
	UInt32 maxColumn = 0;

	for(int row = 0; row < geometry->vRes; row++) {
		for(int col = 0; col < geometry->hRes; col++) {
			fpnColumns[col] += fpnBuffer[row * geometry->hRes + col];
		}
	}

	/* Load 3-point FPN */
	if (gpmc->read16(DISPLAY_GAIN_CONTROL_ADDR) & DISPLAY_GAIN_CONTROL_3POINT) {
		/*
		 * For each column, the sum gives the DC component of the FPN, which
		 * gets applied to the column calibration as the constant term, and
		 * should take ADC gain and curvature into consideration.
		 */
		for (int col = 0; col < geometry->hRes; col++) {
			UInt32 scale = (geometry->vRes * framesToAverage);
			Int64 square = (Int64)fpnColumns[col] * (Int64)fpnColumns[col];
			UInt16 gain = gpmc->read16(COL_GAIN_MEM_START_ADDR + (2 * col));
			Int16 curve = gpmc->read16(COL_CURVE_MEM_START_ADDR + (2 * col));

			/* Column calibration is denoted by:
			 *  f(x) = a*x^2 + b*x + c
			 *
			 * For FPN to output black, f(fpn) = 0 and therefore:
			 *  c = -a*fpn^2 - b*fpn
			 */
			Int64 fpnLinear = -((Int64)fpnColumns[col] * gain);
			Int64 fpnCurved = -(square * curve) / (scale << (COL_CURVE_FRAC_BITS - COL_GAIN_FRAC_BITS));
			Int16 offset = (fpnLinear + fpnCurved) / (scale << COL_GAIN_FRAC_BITS);
			gpmc->write16(COL_OFFSET_MEM_START_ADDR + (2 * col), offset);

#if 0
			fprintf(stderr, "FPN Column %d: gain=%f curve=%f sum=%d, offset=%d\n", col,
					(double)gain / (1 << COL_GAIN_FRAC_BITS),
					(double)curve / (1 << COL_CURVE_FRAC_BITS),
					fpnColumns[col], offset);
#endif

			/* Keep track of the maximum column FPN while we're at it. */
			if ((fpnColumns[col] / scale) > maxColumn) maxColumn = (fpnColumns[col] / scale);
		}

		/* The AC component of each column remains as the per-pixel FPN. */
		for(int row = 0; row < geometry->vRes; row++) {
			for(int col = 0; col < geometry->hRes; col++) {
				int i = row * geometry->hRes + col;
				Int32 fpn = fpnBuffer[i] - (fpnColumns[col] / geometry->vRes);
				writePixelBuf12(pixBuffer, i, (unsigned)(fpn / (int)framesToAverage) & 0xfff);
			}
		}
	}
	/* Load 2-point FPN */
	else {
		for(int i = 0; i < geometry->pixels(); i++) {
			writePixelBuf12(pixBuffer, i, fpnBuffer[i] / framesToAverage);
		}
	}
	gpmc->writeAcqMem((UInt32 *)pixBuffer, FPN_ADDRESS, geometry->size());

	/* Update the image gain to compensate for dynamic range lost to the FPN. */
	imgGain = 4096.0 / (double)((1 << geometry->bitDepth) - maxColumn) * IMAGE_GAIN_FUDGE_FACTOR;
	qDebug() << "imgGain set to" << imgGain;
	setWhiteBalance(whiteBalMatrix);

	free(fpnColumns);
	free(pixBuffer);
}

void Camera::computeFPNCorrection(FrameGeometry *geometry, UInt32 wordAddress, UInt32 framesToAverage, bool writeToFile, bool factory)
{
	UInt32 pixelsPerFrame = geometry->pixels();
	const char *formatStr;
	QString filename;
	std::string fn;
	QFile fp;

	// If writing to file - generate the filename and open for writing
	if(writeToFile)
	{
		//Generate the filename for this particular resolution and offset
		if(factory) {
			formatStr = "cal/factoryFPN/fpn_%dx%doff%dx%d";
		}
		else {
			formatStr = "userFPN/fpn_%dx%doff%dx%d";
		}

		filename.sprintf(formatStr, geometry->hRes, geometry->vRes, geometry->hOffset, geometry->vOffset);
		fn = sensor->getFilename("", ".raw");
		filename.append(fn.c_str());

		qDebug("Writing FPN to file %s", filename.toUtf8().data());

		fp.setFileName(filename);
		fp.open(QIODevice::WriteOnly);
		if(!fp.isOpen())
		{
			qDebug() << "Error: File couldn't be opened";
			return;
		}
	}

	UInt16 * fpnBuffer = (UInt16 *)calloc(pixelsPerFrame, sizeof(UInt16));
	UInt8  * pixBuffer = (UInt8  *)malloc(geometry->size());

	// turn off the sensor
	sensor->seqOnOff(false);

	/* Read frames out of the recorded region and sum their pixels. */
	for(int frame = 0; frame < framesToAverage; frame++) {
		gpmc->readAcqMem((UInt32 *)pixBuffer, wordAddress, geometry->size());
		for(int row = 0; row < geometry->vRes; row++) {
			for(int col = 0; col < geometry->hRes; col++) {
				int i = row * geometry->hRes + col;
				UInt16 pix = readPixelBuf12(pixBuffer, i);
				fpnBuffer[i] += pix;
			}
		}

		/* Advance to the next frame. */
		wordAddress += getFrameSizeWords(geometry);
	}
	loadFPNCorrection(geometry, fpnBuffer, framesToAverage);

	// restart the sensor
	sensor->seqOnOff(true);

	qDebug() << "About to write file...";
	if(writeToFile)
	{
		quint64 retVal;
		for (int i = 0; i < pixelsPerFrame; i++) {
			fpnBuffer[i] /= framesToAverage;
		}
		retVal = fp.write((const char*)fpnBuffer, sizeof(fpnBuffer[0])*pixelsPerFrame);
		if (retVal != (sizeof(fpnBuffer[0])*pixelsPerFrame)) {
			qDebug("Error writing FPN data to file: %s", fp.errorString().toUtf8().data());
		}
		fp.flush();
		fp.close();
	}

	free(fpnBuffer);
	free(pixBuffer);
}

/* Perform zero-time black cal using the calibration recording region. */
Int32 Camera::fastFPNCorrection(void)
{
	struct timespec tRefresh;
	Int32 retVal;

	io->setShutterGatingEnable(false, FLAG_TEMPORARY);
	setIntegrationTime(0.0, &imagerSettings.geometry, SETTING_FLAG_TEMPORARY);
	retVal = setRecSequencerModeCalLoop();
	if (retVal != SUCCESS) {
		io->setShutterGatingEnable(false, FLAG_USESAVED);
		setIntegrationTime(0.0, &imagerSettings.geometry, SETTING_FLAG_USESAVED);
		return retVal;
	}

	/* Activate the recording sequencer and wait for frames. */
	startSequencer();
	ui->setRecLEDFront(true);
	ui->setRecLEDBack(true);
	tRefresh.tv_sec = 0;
	tRefresh.tv_nsec = ((CAL_REGION_FRAMES + 1) * imagerSettings.period) * 10;
	nanosleep(&tRefresh, NULL);

	/* Terminate the calibration recording. */
	terminateRecord();
	ui->setRecLEDFront(false);
	ui->setRecLEDBack(false);
	io->setShutterGatingEnable(false, FLAG_USESAVED);
	setIntegrationTime(0.0, &imagerSettings.geometry, SETTING_FLAG_USESAVED);

	/* Recalculate the black cal. */
	computeFPNCorrection(&imagerSettings.geometry, CAL_REGION_START, CAL_REGION_FRAMES, false, false);
	return SUCCESS;
}

UInt32 Camera::autoFPNCorrection(UInt32 framesToAverage, bool writeToFile, bool noCap, bool factory)
{
	int count;
	const int countMax = 10;
	int ms = 50;
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
//	bool recording;

	if(noCap)
	{
		io->setShutterGatingEnable(false, FLAG_TEMPORARY);
		setIntegrationTime(0.0, &imagerSettings.geometry, SETTING_FLAG_TEMPORARY);
		//nanosleep(&ts, NULL);
	}

	UInt32 retVal;

	qDebug() << "Starting record with a length of" << framesToAverage << "frames";

	CameraRecordModeType oldMode = imagerSettings.mode;
	imagerSettings.mode = RECORD_MODE_FPN;

	retVal = setRecSequencerModeSingleBlock(framesToAverage+1);
	if(SUCCESS != retVal)
	return retVal;

	retVal = startRecording();
	if(SUCCESS != retVal)
		return retVal;

	for(count = 0; (count < countMax) && recording; count++) {nanosleep(&ts, NULL);}

	//Return to set exposure
	if(noCap)
	{
		io->setShutterGatingEnable(false, FLAG_USESAVED);
		setIntegrationTime(0.0, &imagerSettings.geometry, SETTING_FLAG_USESAVED);
	}

	if(count == countMax)	//If after the timeout recording hasn't finished
	{
		qDebug() << "Error: Record failed to stop within timeout period.";

		retVal = stopRecording();
		if(SUCCESS != retVal)
			qDebug() << "Error: Stop Record failed";

		return CAMERA_FPN_CORRECTION_ERROR;
	}

	qDebug() << "Record done, doing normal FPN correction";

	imagerSettings.mode = oldMode;

	computeFPNCorrection(&imagerSettings.geometry, REC_REGION_START, framesToAverage, writeToFile, factory);

	qDebug() << "FPN correction done";
	recordingData.hasBeenSaved = true;

	return SUCCESS;

}

Int32 Camera::loadFPNFromFile(void)
{
	QString filename;
	QFile fp;
	UInt32 retVal = SUCCESS;

	//Generate the filename for this particular resolution and offset
	filename.sprintf("fpn:fpn_%dx%doff%dx%d", getImagerSettings().geometry.hRes, getImagerSettings().geometry.vRes, getImagerSettings().geometry.hOffset, getImagerSettings().geometry.vOffset);

	std::string fn;
	fn = sensor->getFilename("", ".raw");
	filename.append(fn.c_str());
	QFileInfo fpnResFile(filename);
	if (fpnResFile.exists() && fpnResFile.isFile())
		fn = fpnResFile.absoluteFilePath().toLocal8Bit().constData();
	else {
		qDebug("loadFPNFromFile: File not found %s", filename.toLocal8Bit().constData());
		return CAMERA_FILE_NOT_FOUND;
	}

	qDebug() << "Found FPN file" << fn.c_str();

	fp.setFileName(filename);
	fp.open(QIODevice::ReadOnly);
	if(!fp.isOpen())
	{
		qDebug() << "Error: File couldn't be opened";
		return CAMERA_FILE_ERROR;
	}

	UInt32 pixelsPerFrame = imagerSettings.geometry.pixels();
	UInt16 * buffer = new UInt16[pixelsPerFrame];

	//Read in the active region of the FPN data file.
	if (fp.read((char*) buffer, pixelsPerFrame * sizeof(buffer[0])) < (pixelsPerFrame * sizeof(buffer[0]))) {
		retVal = CAMERA_FILE_ERROR;
		goto loadFPNFromFileCleanup;
	}
	loadFPNCorrection(&imagerSettings.geometry, buffer, 1);
	fp.close();

loadFPNFromFileCleanup:
	delete buffer;

	return retVal;
}

Int32 Camera::computeColGainCorrection(UInt32 framesToAverage, bool writeToFile)
{
	UInt32 retVal = SUCCESS;
	UInt32 pixelsPerFrame = recordingData.is.geometry.pixels();
	UInt32 bytesPerFrame = recordingData.is.geometry.size();

	UInt16 * buffer = new UInt16[pixelsPerFrame];
	UInt16 * fpnBuffer = new UInt16[pixelsPerFrame];
	UInt32 * rawBuffer32 = new UInt32[bytesPerFrame / 4];
	UInt8 * rawBuffer = (UInt8 *)rawBuffer32;

	QString filename;
	QFile fp;

	int i;
	double minVal = 0;
	double valueSum[sensor->getHResIncrement()] = {0.0};
	double gainCorrection[sensor->getHResIncrement()];

	// If writing to file - generate the filename and open for writing
	if(writeToFile)
	{
		//Generate the filename for this particular resolution and offset
		if(recordingData.is.gain >= LUX1310_GAIN_4)
			filename.append("cal/dcgH.bin");
		else
			filename.append("cal/dcgL.bin");

		qDebug("Writing colGain to file %s", filename.toUtf8().data());

		fp.setFileName(filename);
		fp.open(QIODevice::WriteOnly);
		if(!fp.isOpen())
		{
			qDebug("Error: File couldn't be opened");
			retVal = CAMERA_FILE_ERROR;
			goto computeColGainCorrectionCleanup;
		}
	}

	recordFrames(1);

	//Zero buffer
	memset(buffer, 0, sizeof(buffer)*sizeof(buffer[0]));

	//Read the FPN frame into a buffer
	gpmc->readAcqMem(rawBuffer32, FPN_ADDRESS, bytesPerFrame);

	//Retrieve pixels from the raw buffer and sum them
	for(i = 0; i < pixelsPerFrame; i++)
	{
		fpnBuffer[i] = readPixelBuf12(rawBuffer, i);
	}

	//Sum pixel values across frames
	for(int frame = 0; frame < framesToAverage; frame++)
	{
		//Get one frame into the raw buffer
		gpmc->readAcqMem(rawBuffer32,
				   REC_REGION_START + frame * getFrameSizeWords(&recordingData.is.geometry),
				   bytesPerFrame);

		//Retrieve pixels from the raw buffer and sum them
		for(i = 0; i < pixelsPerFrame; i++)
		{
			buffer[i] += readPixelBuf12(rawBuffer, i) - fpnBuffer[i];
		}
	}

	if(isColor)
	{
		//Divide by number summed to get average and write to FPN area
		for(i = 0; i < recordingData.is.geometry.hRes; i++)
		{
			bool isGreen = sensor->getFilterColor(i, recordingData.is.geometry.vRes / 2) == FILTER_COLOR_GREEN;
			UInt32 y = recordingData.is.geometry.vRes / 2 + (isGreen ? 0 : 1);	//Zig zag to we only look at green pixels
			valueSum[i % sensor->getHResIncrement()] += (double)buffer[i+recordingData.is.geometry.hRes * y] / (double)framesToAverage;
		}
	}
	else
	{
		//Divide by number summed to get average and write to FPN area
		for(i = 0; i < recordingData.is.geometry.hRes; i++)
		{
			valueSum[i % sensor->getHResIncrement()] += (double)buffer[i + recordingData.is.geometry.pixels() / 2] / (double)framesToAverage;
		}
	}

	for(i = 0; i < sensor->getHResIncrement(); i++)
	{
		//divide by number of values summed to get pixel value
		valueSum[i] /= ((double)recordingData.is.geometry.hRes/((double)sensor->getHResIncrement()));

		//Find min value
		if(valueSum[i] > minVal)
			minVal = valueSum[i];
	}

	qDebug() << "Gain correction values:";
	for(i = 0; i < sensor->getHResIncrement(); i++)
	{
		gainCorrection[i] =  minVal / valueSum[i];


		qDebug() << gainCorrection[i];

	}

	//Check that values are within a sane range
	for(i = 0; i < sensor->getHResIncrement(); i++)
	{
		if(gainCorrection[i] < LUX1310_GAIN_CORRECTION_MIN || gainCorrection[i] > LUX1310_GAIN_CORRECTION_MAX) {
			retVal = CAMERA_GAIN_CORRECTION_ERROR;
			goto computeColGainCorrectionCleanup;
		}
	}

	if(writeToFile)
	{
		quint64 retVal64;
		retVal64 = fp.write((const char*)gainCorrection, sizeof(gainCorrection[0])*sensor->getHResIncrement());
		if (retVal64 != (sizeof(gainCorrection[0])*sensor->getHResIncrement())) {
			qDebug("Error writing colGain data to file: %s (%d vs %d)", fp.errorString().toUtf8().data(), (UInt32) retVal64, sizeof(gainCorrection[0])*sensor->getHResIncrement());
		}
		fp.flush();
		fp.close();
	}

	for(i = 0; i < recordingData.is.geometry.hRes; i++)
		gpmc->write16(COL_GAIN_MEM_START_ADDR+2*i, gainCorrection[i % sensor->getHResIncrement()]*4096.0);

computeColGainCorrectionCleanup:
	delete[] buffer;
	delete[] fpnBuffer;
	delete[] rawBuffer32;
	return retVal;
}

/*===============================================================
 * checkForDeadPixels
 *
 * This records a set of frames at full resolution over a few
 * exposure levels then averages them to find the per-pixel
 * deviation.
 *
 * Pass/Fail is returned
 */
#define BAD_PIXEL_RING_SIZE          6
#define DEAD_PIXEL_THRESHHOLD        512
#define MAX_DEAD_PIXELS              0
Int32 Camera::checkForDeadPixels(int* resultCount, int* resultMax) {
	Int32 retVal;

	int exposureSet;
	UInt32 nomExp;

	int i;
	int x, y;

	int averageQuad[4];
	int quad[4];
	int dividerValue;

	int totalFailedPixels = 0;

	ImagerSettings_t _is;

	int frame;
	int stride;
	int lx, ly;
	int endX, endY;
	int yOffset;

	int maxOffset = 0;
	int averageOffset = 0;
	int pixelsInFrame = 0;

	double exposures[] = {0.001,
						  0.125,
						  0.25,
						  0.5,
						  1};

	qDebug("===========================================================================");
	qDebug("Starting dead pixel detection");

	_is.geometry = sensor->getMaxGeometry();
	_is.geometry.vDarkRows = 0;	//inactive dark rows
	_is.exposure = 400000;		//10ns increments
	_is.period = 500000;		//Frame period in 10ns increments
	_is.gain = sensor->getMinGain();
	_is.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&_is.geometry);
	_is.disableRingBuffer = 0;
	_is.mode = RECORD_MODE_NORMAL;
	_is.prerecordFrames = 1;
	_is.segmentLengthFrames = imagerSettings.recRegionSizeFrames;
	_is.segments = 1;
	_is.temporary = 1;

	retVal = setImagerSettings(_is);
	if(SUCCESS != retVal) {
		qDebug("error during setImagerSettings");
		return retVal; // this happens before buffers are made
	}

	UInt32 pixelsPerFrame = imagerSettings.geometry.pixels();
	UInt32 bytesPerFrame = imagerSettings.geometry.size();

	UInt16* buffer = new UInt16[pixelsPerFrame];
	UInt16* fpnBuffer = new UInt16[pixelsPerFrame];
	UInt32* rawBuffer32 = new UInt32[(bytesPerFrame+3) >> 2];
	UInt8* rawBuffer = (UInt8*)rawBuffer32;


	memset(fpnBuffer, 0, sizeof(fpnBuffer));
	//Read the FPN frame into a buffer
	gpmc->readAcqMem(rawBuffer32, FPN_ADDRESS, bytesPerFrame);

	//Retrieve pixels from the raw buffer and sum them
	for(int i = 0; i < pixelsPerFrame; i++) {
		fpnBuffer[i] = readPixelBuf12(rawBuffer, i);
	}

	retVal = adjustExposureToValue(CAMERA_MAX_EXPOSURE_TARGET, 100, false);
	if(SUCCESS != retVal) {
		qDebug("error during adjustExposureToValue");
		goto checkForDeadPixelsCleanup;
	}

	nomExp = imagerSettings.exposure;

	//For each exposure value
	for(exposureSet = 0; exposureSet < (sizeof(exposures)/sizeof(exposures[0])); exposureSet++) {
		//Set exposure
		_is.exposure = (UInt32)((double)nomExp * exposures[exposureSet]);
		averageOffset = 0;

		retVal = setImagerSettings(_is);
		if(SUCCESS != retVal) {
			qDebug("error during setImagerSettings");
			goto checkForDeadPixelsCleanup;
		}

		qDebug("Recording frames for exposure 1/%ds", 100000000 / _is.exposure);
		//Record frames
		retVal = recordFrames(16);
		if(SUCCESS != retVal) {
			qDebug("error during recordFrames");
			goto checkForDeadPixelsCleanup;
		}

		//Zero buffer
		memset(buffer, 0, sizeof(buffer));



		// Average pixels across frame
		for(frame = 0; frame < 16; frame++) {
			//Get one frame into the raw buffer
			UInt32 frameAddr = REC_REGION_START + frame * getFrameSizeWords(&recordingData.is.geometry);
			gpmc->readAcqMem(rawBuffer32, frameAddr, bytesPerFrame);

			//Retrieve pixels from the raw buffer and sum them
			for(i = 0; i < pixelsPerFrame; i++) {
				buffer[i] += readPixelBuf12(rawBuffer, i) - fpnBuffer[i];
			}
		}
		for(i = 0; i < pixelsPerFrame; i++) {
			buffer[i] >>= 4;
		}

		// take average quad
		averageQuad[0] = averageQuad[1] = averageQuad[2] = averageQuad[3] = 0;
		for (y = 0; y < recordingData.is.geometry.vRes-2; y += 2) {
			stride = _is.geometry.hRes;
			yOffset = y * stride;
			for (x = 0; x < recordingData.is.geometry.hRes-2; x += 2) {
				averageQuad[0] += buffer[x   + yOffset       ];
				averageQuad[1] += buffer[x+1 + yOffset       ];
				averageQuad[2] += buffer[x   + yOffset+stride];
				averageQuad[3] += buffer[x+1 + yOffset+stride];
			}
		}
		averageQuad[0] /= (pixelsPerFrame>>2);
		averageQuad[1] /= (pixelsPerFrame>>2);
		averageQuad[2] /= (pixelsPerFrame>>2);
		averageQuad[3] /= (pixelsPerFrame>>2);

		qDebug("bad pixel detection - average for frame: 0x%04X, 0x%04X, 0x%04X, 0x%04X", averageQuad[0], averageQuad[1], averageQuad[2], averageQuad[3]);
		// note that the average isn't actually used after this point - the variables are reused later

		//dividerValue = (1<<16) / (((BAD_PIXEL_RING_SIZE*2) * (BAD_PIXEL_RING_SIZE*2))/2);

		// Check if pixel is valid
		for (y = 0; y < recordingData.is.geometry.vRes-2; y += 2) {
			for (x = 0; x < recordingData.is.geometry.hRes-2; x += 2) {
				quad[0] = quad[1] = quad[2] = quad[3] = 0;
				averageQuad[0] = averageQuad[1] = averageQuad[2] = averageQuad[3] = 0;

				dividerValue = 0;
				for (ly = -BAD_PIXEL_RING_SIZE; ly <= BAD_PIXEL_RING_SIZE+1; ly+=2) {
					stride = _is.geometry.hRes;
					endY = y + ly;
					if (endY >= 0 && endY < recordingData.is.geometry.vRes-2)
						yOffset = endY * _is.geometry.hRes;
					else
						yOffset = y * _is.geometry.hRes;

					for (lx = -BAD_PIXEL_RING_SIZE; lx <= BAD_PIXEL_RING_SIZE+1; lx+=2) {
						endX = x + lx;
						if (endX >= 0 && endX < recordingData.is.geometry.hRes-2) {
							averageQuad[0] += buffer[endX   + yOffset       ];
							averageQuad[1] += buffer[endX+1 + yOffset       ];
							averageQuad[2] += buffer[endX   + yOffset+stride];
							averageQuad[3] += buffer[endX+1 + yOffset+stride];
							dividerValue++;
						}
					}
				}
				dividerValue = (1<<16) / dividerValue;
				// now divide the outcome by the number of pixels
				//
				averageQuad[0] = (averageQuad[0] * dividerValue);
				averageQuad[0] >>= 16;

				averageQuad[1] = (averageQuad[1] * dividerValue);
				averageQuad[1] >>= 16;

				averageQuad[2] = (averageQuad[2] * dividerValue);
				averageQuad[2] >>= 16;

				averageQuad[3] = (averageQuad[3] * dividerValue);
				averageQuad[3] >>= 16;

				yOffset = y * _is.geometry.hRes;

				quad[0] = averageQuad[0] - buffer[x      + yOffset       ];
				quad[1] = averageQuad[1] - buffer[x   +1 + yOffset       ];
				quad[2] = averageQuad[2] - buffer[x      + yOffset+stride];
				quad[3] = averageQuad[3] - buffer[x   +1 + yOffset+stride];

				if (quad[0] > maxOffset) maxOffset = quad[0];
				if (quad[1] > maxOffset) maxOffset = quad[1];
				if (quad[2] > maxOffset) maxOffset = quad[2];
				if (quad[3] > maxOffset) maxOffset = quad[3];
				if (quad[0] < -maxOffset) maxOffset = -quad[0];
				if (quad[1] < -maxOffset) maxOffset = -quad[1];
				if (quad[2] < -maxOffset) maxOffset = -quad[2];
				if (quad[3] < -maxOffset) maxOffset = -quad[3];

				pixelsInFrame++;
				averageOffset += quad[0] + quad[1] + quad[2] + quad[3];

				if (quad[0] > DEAD_PIXEL_THRESHHOLD || quad[0] < -DEAD_PIXEL_THRESHHOLD) {
					//qDebug("Bad pixel found: %dx%d (0x%04X vs 0x%04X in the local area)", x  , y  , buffer[x      + yOffset       ], averageQuad[0]);
					totalFailedPixels++;
				}
				if (quad[1] > DEAD_PIXEL_THRESHHOLD || quad[1] < -DEAD_PIXEL_THRESHHOLD) {
					//qDebug("Bad pixel found: %dx%d (0x%04X vs 0x%04X in the local area)", x+1, y  , buffer[x   +1 + yOffset       ], averageQuad[1]);
					totalFailedPixels++;
				}
				if (quad[2] > DEAD_PIXEL_THRESHHOLD || quad[2] < -DEAD_PIXEL_THRESHHOLD) {
					//qDebug("Bad pixel found: %dx%d (0x%04X vs 0x%04X in the local area)", x  , y+1, buffer[x      + yOffset+stride], averageQuad[2]);
					totalFailedPixels++;
				}
				if (quad[3] > DEAD_PIXEL_THRESHHOLD || quad[3] < -DEAD_PIXEL_THRESHHOLD) {
					//qDebug("Bad pixel found: %dx%d (0x%04X vs 0x%04X in the local area)", x+1, y+1, buffer[x   +1 + yOffset+stride], averageQuad[3]);
					totalFailedPixels++;
				}
			}
		}

		averageOffset /= pixelsInFrame;
		qDebug("===========================================================================");
		qDebug("Average offset for exposure 1/%ds: %d", 100000000 / _is.exposure, averageOffset);
		qDebug("===========================================================================");
		pixelsInFrame = 0;
		averageOffset = 0;
	}
	qDebug("Total dead pixels found: %d", totalFailedPixels);
	if (totalFailedPixels > MAX_DEAD_PIXELS) {
		retVal = CAMERA_DEAD_PIXEL_FAILED;
		goto checkForDeadPixelsCleanup;
	}
	retVal = SUCCESS;

checkForDeadPixelsCleanup:
	delete buffer;
	delete fpnBuffer;
	delete rawBuffer32;
	qDebug("===========================================================================");
	if (resultMax != NULL) *resultMax = maxOffset;
	if (resultCount != NULL) *resultCount = totalFailedPixels;
	return retVal;
}

void Camera::computeFPNColumns(FrameGeometry *geometry, UInt32 wordAddress, UInt32 framesToAverage)
{
	UInt32 rowSize = (geometry->hRes * BITS_PER_PIXEL) / 8;
	UInt32 scale = (geometry->vRes * framesToAverage);
	UInt32 *pxBuffer = (UInt32 *)malloc(rowSize * geometry->vRes);
	UInt32 *fpnColumns = (UInt32 *)calloc(geometry->hRes, sizeof(UInt32));

	/* Read and sum the dark columns */
	for (int i = 0; i < framesToAverage; i++) {
		gpmc->readAcqMem(pxBuffer, wordAddress, rowSize * geometry->vRes);
		for (int row = 0; row < geometry->vRes; row++) {
			for(int col = 0; col < geometry->hRes; col++) {
				fpnColumns[col] += readPixelBuf12((UInt8 *)pxBuffer, row * geometry->hRes + col);
			}
		}
		wordAddress += getFrameSizeWords(geometry);
	}
	/* Write the average value for each column */
	for (int col = 0; col < geometry->hRes; col++) {
		UInt16 gain = gpmc->read16(COL_GAIN_MEM_START_ADDR + (2 * col));
		Int32 offset = ((UInt64)fpnColumns[col] * gain) / (scale << COL_GAIN_FRAC_BITS);
		fprintf(stderr, "FPN Column %d: fpn=%d, gain=%f correction=%d\n", col, fpnColumns[col], (double)gain / (1 << COL_GAIN_FRAC_BITS), -offset);
		gpmc->write16(COL_OFFSET_MEM_START_ADDR + (2 * col), -offset & 0xffff);
	}
	/* Clear out the per-pixel FPN (for now). */
	memset(pxBuffer, 0, rowSize * geometry->vRes);
	gpmc->writeAcqMem(pxBuffer, FPN_ADDRESS, rowSize * geometry->vRes);
	free(pxBuffer);
	free(fpnColumns);
}

/* Compute the gain columns using the analog test voltage. */
void Camera::computeGainColumns(FrameGeometry *geometry, UInt32 wordAddress, const struct timespec *interval, const char *gName)
{
	UInt32 numRows = 64;
	UInt32 numChannels = sensor->getHResIncrement();
	UInt32 scale = numRows * geometry->hRes / numChannels;
	UInt32 pixFullScale = (1 << BITS_PER_PIXEL);
	UInt32 rowStart = ((geometry->vRes - numRows) / 2) & ~0x1f;
	UInt32 rowSize = (geometry->hRes * BITS_PER_PIXEL) / 8;
	UInt32 *pxBuffer = (UInt32 *)malloc(numRows * rowSize);
	UInt32 highColumns[numChannels] = {0};
	UInt32 midColumns[numChannels] = {0};
	UInt32 lowColumns[numChannels] = {0};
	UInt16 colGain[numChannels];
	Int16 colCurve[numChannels];
	int col;
	UInt32 maxColumn, minColumn;
	unsigned int vhigh, vlow, vmid;
	unsigned int vmax;

	/* Setup the default calibration */
	for (col = 0; col < numChannels; col++) {
		colGain[col] = (1 << COL_GAIN_FRAC_BITS);
		colCurve[col] = 0;
	}

	/* Enable analog test mode. */
	vmax = sensor->enableAnalogTestMode();
	if (!vmax) {
		qWarning("Warning! ADC Auto calibration not supported.");
		goto cleanup;
	}

	/* Sample rows from somewhere around the middle of the frame. */
	wordAddress += (rowSize * rowStart) / BYTES_PER_WORD;

	/* Search for a dummy voltage high reference point. */
	for (vhigh = 31; vhigh > 0; vhigh--) {
		sensor->setAnalogTestVoltage(vhigh);
		nanosleep(interval, NULL);

		/* Get the average pixel value. */
		gpmc->readAcqMem(pxBuffer, wordAddress, rowSize * numRows);
		memset(highColumns, 0, sizeof(highColumns));
		for (int row = 0; row < numRows; row++) {
			for(int col = 0; col < geometry->hRes; col++) {
				highColumns[col % numChannels] += readPixelBuf12((UInt8 *)pxBuffer, row * geometry->hRes + col);
			}
		}
		maxColumn = 0;
		for (col = 0; col < numChannels; col++) {
			if (highColumns[col] > maxColumn) maxColumn = highColumns[col];
		}
		maxColumn /= scale;

		/* High voltage should be less than 3/4 of full scale */
		if (maxColumn <= (pixFullScale - (pixFullScale / 8))) {
			break;
		}
	}

	/* Search for a dummy voltage low reference point. */
	for (vlow = 0; vlow < vhigh; vlow++) {
		sensor->setAnalogTestVoltage(vlow);
		nanosleep(interval, NULL);

		/* Get the average pixel value. */
		gpmc->readAcqMem(pxBuffer, wordAddress, rowSize * numRows);
		memset(lowColumns, 0, sizeof(lowColumns));
		for (int row = 0; row < numRows; row++) {
			for(col = 0; col < geometry->hRes; col++) {
				lowColumns[col % numChannels] += readPixelBuf12((UInt8 *)pxBuffer, row * geometry->hRes + col);
			}
		}
		minColumn = UINT32_MAX;
		for (col = 0; col < numChannels; col++) {
			if (lowColumns[col] < minColumn) minColumn = lowColumns[col];
		}
		minColumn /= scale;

		/* Find the minimum voltage that does not clip. */
		if (minColumn >= COL_OFFSET_FOOTROOM) {
			break;
		}
	}

	/* Sample the midpoint, which should be around quarter scale. */
	vmid = (vhigh + 3*vlow) / 4;
	sensor->setAnalogTestVoltage(vmid);
	nanosleep(interval, NULL);
	fprintf(stderr, "ADC Calibration voltages: vlow=%d vmid=%d vhigh=%d\n", vlow, vmid, vhigh);

	/* Get the average pixel value. */
	gpmc->readAcqMem(pxBuffer, wordAddress, rowSize * numRows);
	memset(midColumns, 0, sizeof(midColumns));
	for (int row = 0; row < numRows; row++) {
		for(int col = 0; col < geometry->hRes; col++) {
			midColumns[col % numChannels] += readPixelBuf12((UInt8 *)pxBuffer, row * geometry->hRes + col);
		}
	}
	free(pxBuffer);

	/* Determine which column has the highest response, and sanity check the gain measurements. */
	maxColumn = 0;
	for (col = 0; col < numChannels; col++) {
		UInt32 minrange = (pixFullScale * scale / 16);
		UInt32 diff = highColumns[col] - lowColumns[col];
		if (highColumns[col] <= (midColumns[col] + minrange)) break;
		if (midColumns[col] <= (lowColumns[col] + minrange)) break;
		if (diff > maxColumn) maxColumn = diff;
	}
	if (col != numChannels) {
		qWarning("Warning! ADC Auto calibration range error.");
		goto cleanup;
	}

	/* Compute the 3-point gain calibration coefficients. */
	for (int col = 0; col < numChannels; col++) {
		/* Compute the 2-point calibration coefficients. */
		UInt32 diff = highColumns[col] - lowColumns[col];
		UInt32 gain2pt = ((unsigned long long)maxColumn << COL_GAIN_FRAC_BITS) / diff;
#if USE_3POINT_CAL
		/* Predict the ADC to be linear with dummy voltage and find the error. */
		UInt32 predict = lowColumns[col] + (diff * (vmid - vlow)) / (vhigh - vlow);
		Int32 err2pt = (int)(midColumns[col] - predict);

		/*
		 * Add a parabola to compensate for the curvature. This parabola should have
		 * zeros at the high and low measurement points, and a curvature to compensate
		 * for the error at the middle range. Such a parabola is therefore defined by:
		 *
		 *	f(X) = a*(X - Xlow)*(X - Xhigh), and
		 *  f(Xmid) = -error
		 *
		 * Solving for the curvature gives:
		 *
		 *  a = error / ((Xmid - Xlow) * (Xhigh - Xmid))
		 *
		 * The resulting 3-point calibration function is therefore:
		 *
		 *  Y = a*X^2 + (b - a*Xhigh - a*Xlow)*X + c
		 *  a = three-point curvature correction.
		 *  b = two-point gain correction.
		 *  c = some constant (black level).
		 */
		Int64 divisor = (UInt64)(midColumns[col] - lowColumns[col]) * (UInt64)(highColumns[col] - midColumns[col]) / scale;
		Int32 curve3pt = ((Int64)err2pt << COL_CURVE_FRAC_BITS) / divisor;
		Int32 gainadj = ((Int64)curve3pt * (highColumns[col] + lowColumns[col])) / scale;
		colGain[col] = gain2pt - (gainadj >> (COL_CURVE_FRAC_BITS - COL_GAIN_FRAC_BITS));
		colCurve[col] = curve3pt;

#if 0
		fprintf(stderr, "ADC Column %d 3-point values: 0x%03x, 0x%03x, 0x%03x\n",
						col, lowColumns[col], midColumns[col], highColumns[col]);
		fprintf(stderr, "ADC Column %d 2-point cal: gain2pt=%f predict=0x%03x err2pt=%d\n", col,
						(double)gain2pt / (1 << COL_GAIN_FRAC_BITS), predict, err2pt);
		fprintf(stderr, "ADC Column %d 3-point cal: gain3pt=%f curve3pt=%.9f\n", col,
						(double)colGain[col] / (1 << COL_GAIN_FRAC_BITS),
						(double)colCurve[col] / (1 << COL_CURVE_FRAC_BITS));
		fprintf(stderr, "ADC Column %d 3-point registers: gainadj=%d gain=0x%04x curve=0x%04x\n", col, gainadj, colGain[col], (unsigned)colCurve[col] & 0xffff);
#endif
#else
		/* Apply 2-point calibration */
		colGain[col] = gain2pt;
		colCurve[col] = 0;
#endif
	}

	/* Save the gain calibration data to a file. */
	if (gName) {
		double gainCorrection[numChannels];
		double curveCorrection[numChannels];
		QString filename;
		QFile fp;

		for (int col = 0; col < numChannels; col++) {
			gainCorrection[col] = (double)colGain[col] / (1 << COL_GAIN_FRAC_BITS);
			curveCorrection[col] = (double)colCurve[col] / (1 << COL_CURVE_FRAC_BITS);
		}

		/* Save column gain data. */
		filename.sprintf("cal/colGain_%s.bin", gName);
		fp.setFileName(filename);
		fp.open(QIODevice::WriteOnly);
		if(fp.isOpen()) {
			fp.write((const char*)gainCorrection, sizeof(gainCorrection));
			fp.flush();
			fp.close();
		}

#if USE_3POINT_CAL
		/* Save column curvature data. */
		filename.sprintf("cal/colCurve_%s.bin", gName);
		fp.setFileName(filename);
		fp.open(QIODevice::WriteOnly);
		if(fp.isOpen()) {
			fp.write((const char*)curveCorrection, sizeof(curveCorrection));
			fp.flush();
			fp.close();
		}
#endif
	}

cleanup:
	sensor->disableAnalogTestMode();

	/* Enable 3-point calibration and load the column gains */
#if USE_3POINT_CAL
	gpmc->write16(DISPLAY_GAIN_CONTROL_ADDR, DISPLAY_GAIN_CONTROL_3POINT);
#endif
	for (int col = 0; col < geometry->hRes; col++) {
		gpmc->write16(COL_GAIN_MEM_START_ADDR + (2 * col), colGain[col % numChannels]);
		gpmc->write16(COL_CURVE_MEM_START_ADDR + (2 * col), colCurve[col % numChannels]);
	}
}

Int32 Camera::autoOffsetCalibration(unsigned int iterations)
{
	ImagerSettings_t isPrev = imagerSettings;
	ImagerSettings_t isDark;
	FrameGeometry isMaxSize = sensor->getMaxGeometry();
	Int32 retVal;

	/* Swap the black rows into the top of the frame. */
	memcpy(&isDark, &isPrev, sizeof(isDark));
#if 0
	if (!isDark.geometry.vDarkRows) {
		isDark.geometry.vDarkRows = isMaxSize.vDarkRows / 2;
		isDark.geometry.vRes -= isDark.geometry.vDarkRows;
		isDark.geometry.vOffset += isDark.geometry.vDarkRows;
	}
#endif
	isDark.recRegionSizeFrames = CAL_REGION_FRAMES;
	isDark.disableRingBuffer = 0;
	isDark.mode = RECORD_MODE_NORMAL;
	isDark.prerecordFrames = 1;
	isDark.segmentLengthFrames = CAL_REGION_FRAMES;
	isDark.segments = 1;
	isDark.temporary = 1;
	retVal = setImagerSettings(isDark);
	if(SUCCESS != retVal) {
		return retVal;
	}

	retVal = setRecSequencerModeCalLoop();
	if (SUCCESS != retVal) {
		setImagerSettings(isPrev);
		return retVal;
	}

	/* Activate the recording sequencer. */
	startSequencer();
	ui->setRecLEDFront(true);
	ui->setRecLEDBack(true);

	/* Run the ADC training algorithm. */
	sensor->adcOffsetTraining(&isDark.geometry, CAL_REGION_START, CAL_REGION_FRAMES);

	terminateRecord();
	ui->setRecLEDFront(false);
	ui->setRecLEDBack(false);

	/* Restore the sensor settings. */
	return setImagerSettings(isPrev);
}

Int32 Camera::autoGainCalibration(unsigned int iterations)
{
	struct timespec tRefresh;
	char gName[16];
	Int32 retVal;

	snprintf(gName, sizeof(gName), "G%d", imagerSettings.gain);

	/* Record frames continuously into a small loop buffer. */
	retVal = setRecSequencerModeCalLoop();
	if (SUCCESS != retVal) {
		return retVal;
	}

	/* Activate the recording sequencer. */
	startSequencer();
	ui->setRecLEDFront(true);
	ui->setRecLEDBack(true);

	/* Run the gain calibration algorithm. */
	tRefresh.tv_sec = 0;
	tRefresh.tv_nsec = (CAL_REGION_FRAMES+10) * (imagerSettings.period * 1000000000ULL) / sensor->getFramePeriodClock();
	computeGainColumns(&imagerSettings.geometry, CAL_REGION_START, &tRefresh, gName);

	terminateRecord();
	ui->setRecLEDFront(false);
	ui->setRecLEDBack(false);

	return SUCCESS;
}

void Camera::loadColGainFromFile(void)
{
	QString filename;
	UInt32 numChannels = sensor->getHResIncrement();
	double gainCorrection[numChannels];
	double curveCorrection[numChannels];

	/* Prepare a sensible default gain. */
	for (int col = 0; col < numChannels; col++) {
		gainCorrection[col] = 1.0;
		curveCorrection[col] = 0.0;
	}

	/* Load gain correction. */
	filename.sprintf("cal:colGain_G%d.bin", imagerSettings.gain);
	QFileInfo colGainFile(filename);
	if (!colGainFile.exists() || !colGainFile.isFile()) {
		/* Fall back to legacy 2-point gain files. */
		filename = (imagerSettings.gain < 4) ? "cal:dcgL.bin" : "cal:dcgH.bin";
		colGainFile.setFile(filename);
	}
	if (colGainFile.exists() && colGainFile.isFile()) {
		QFile fp;

		qDebug("Found colGain file %s", colGainFile.absoluteFilePath().toLocal8Bit().constData());

		fp.setFileName(filename);
		fp.open(QIODevice::ReadOnly);
		qint64 ret = fp.read((char*)gainCorrection, sizeof(gainCorrection));
		if (ret < sizeof(gainCorrection)) {
			qDebug("Error: File couldn't be opened (ret=%d)", (int)ret);
		}
		fp.close();
	}
	for (int col = 0; col < imagerSettings.geometry.hRes; col++) {
		gpmc->write16(COL_GAIN_MEM_START_ADDR + (2 * col), (int)(gainCorrection[col % numChannels] * (1 << COL_GAIN_FRAC_BITS)));
	}

#if USE_3POINT_CAL
	/* Load curvature correction. */
	filename.sprintf("cal:colCurve_G%d.bin", imagerSettings.gain);
	QFileInfo colCurveFile(filename);
	if (colCurveFile.exists() && colCurveFile.isFile()) {
		QFile fp;

		qDebug("Found colCurve file %s", colCurveFile.absoluteFilePath().toLocal8Bit().constData());

		fp.setFileName(filename);
		fp.open(QIODevice::ReadOnly);
		qint64 ret = fp.read((char*)curveCorrection, sizeof(curveCorrection));
		if (ret < sizeof(curveCorrection)) {
			qDebug("Error: File couldn't be opened (ret=%d)", (int)ret);
		}
		fp.close();
	}
	for (int col = 0; col < imagerSettings.geometry.hRes; col++) {
		Int16 curve16 = curveCorrection[col % numChannels] * (1 << COL_CURVE_FRAC_BITS);
		gpmc->write16(COL_CURVE_MEM_START_ADDR + (2 * col), (unsigned)curve16);
	}

	/* Enable 3-point calibration. */
	gpmc->write16(DISPLAY_GAIN_CONTROL_ADDR, DISPLAY_GAIN_CONTROL_3POINT);
#else
	/* Disable 3-point calibration. */
	gpmc->write16(DISPLAY_GAIN_CONTROL_ADDR, 0);
#endif
}

/* Compute the gain columns by controlling exposure. */
void Camera::factoryGainColumns(FrameGeometry *geometry, UInt32 wordAddress, const struct timespec *interval)
{
	UInt32 numRows = 64;
	UInt32 numChannels = sensor->getHResIncrement();
	UInt32 scale = numRows * geometry->hRes / (numChannels * 2);
	UInt32 pixFullScale = (1 << BITS_PER_PIXEL);
	UInt32 rowStart = ((geometry->vRes - numRows) / 2) & ~0x1f;
	UInt32 rowSize = (geometry->hRes * BITS_PER_PIXEL) / 8;
	UInt32 *pxBuffer = (UInt32 *)malloc(numRows * rowSize);
	UInt32 highColumns[numChannels] = {0};
	UInt32 lowColumns[numChannels] = {0};
	UInt16 colGain[numChannels];
	int col;
	UInt32 maxColumn, minColumn;
	UInt32 expHigh;
	UInt32 fPeriod = sensor->getFramePeriod();
	UInt32 expStart = sensor->getIntegrationTime();
	UInt32 expLow = sensor->getMinIntegrationTime(fPeriod, geometry);
	UInt32 expMax = sensor->getMaxIntegrationTime(fPeriod, geometry);
	UInt32 expStep = (expMax - expLow) / 1000;

	/* Setup the default calibration */
	for (col = 0; col < numChannels; col++) {
		colGain[col] = (1 << COL_GAIN_FRAC_BITS);
	}

	/* Sample rows from somewhere around the middle of the frame. */
	wordAddress += (rowSize * rowStart) / BYTES_PER_WORD;

	/* Search for a dummy voltage high reference point. */
	for (expHigh = expMax; expHigh > 0; expHigh -= expStep) {
		sensor->setIntegrationTime(expHigh, geometry);
		nanosleep(interval, NULL);

		/* Get the average value for only green pixels. */
		gpmc->readAcqMem(pxBuffer, wordAddress, rowSize * numRows);
		memset(highColumns, 0, sizeof(highColumns));
		for (int row = 0; row < numRows; row += 2) {
			for(int col = 0; col < geometry->hRes; col++) {
				highColumns[col % numChannels] += readPixelBuf12((UInt8 *)pxBuffer, (row + (col&1)) * geometry->hRes + col);
			}
		}
		maxColumn = 0;
		for (col = 0; col < numChannels; col++) {
			if (highColumns[col] > maxColumn) maxColumn = highColumns[col];
		}
		maxColumn /= scale;

		/* High voltage should be less than 3/4 of full scale */
		if (maxColumn <= (pixFullScale - (pixFullScale / 8))) {
			break;
		}
	}

	sensor->setIntegrationTime(expLow, geometry);
	nanosleep(interval, NULL);

	/* Get the average pixel value. */
	gpmc->readAcqMem(pxBuffer, wordAddress, rowSize * numRows);
	memset(lowColumns, 0, sizeof(lowColumns));
	for (int row = 0; row < numRows; row += 2) {
		for(col = 0; col < geometry->hRes; col++) {
			lowColumns[col % numChannels] += readPixelBuf12((UInt8 *)pxBuffer, (row + (col&1)) * geometry->hRes + col);
		}
	}
	minColumn = UINT32_MAX;
	for (col = 0; col < numChannels; col++) {
		if (lowColumns[col] < minColumn) minColumn = lowColumns[col];
	}
	minColumn /= scale;
	free(pxBuffer);

	/* Determine which column has the highest response, and sanity check the gain measurements. */
	maxColumn = 0;
	for (col = 0; col < numChannels; col++) {
		UInt32 minrange = (pixFullScale * scale / 16);
		UInt32 diff = highColumns[col] - lowColumns[col];
		if (diff < minrange) break;
		if (diff > maxColumn) maxColumn = diff;
	}
	if (col != numChannels) {
		qWarning("Warning! ADC gain calibration range error.");
		goto cleanup;
	}

	/* Compute the 3-point gain calibration coefficients. */
	for (int col = 0; col < numChannels; col++) {
		/* Compute the 2-point calibration coefficients. */
		UInt32 diff = highColumns[col] - lowColumns[col];
		UInt32 gain2pt = ((unsigned long long)maxColumn << COL_GAIN_FRAC_BITS) / diff;

		/* Apply 2-point calibration */
		colGain[col] = gain2pt;
	}

cleanup:
	sensor->setIntegrationTime(expStart, geometry);

	/* Enable 3-point calibration and load the column gains */
	gpmc->write16(DISPLAY_GAIN_CONTROL_ADDR, DISPLAY_GAIN_CONTROL_3POINT);
	for (int col = 0; col < geometry->hRes; col++) {
		gpmc->write16(COL_GAIN_MEM_START_ADDR + (2 * col), colGain[col % numChannels]);
		gpmc->write16(COL_CURVE_MEM_START_ADDR + (2 * col), 0);
	}
}

Int32 Camera::autoColGainCorrection(void)
{
#if 0
	Int32 retVal;
	ImagerSettings_t _is;

	_is.geometry = sensor->getMaxGeometry();
	_is.geometry.vDarkRows = 0;	//inactive dark rows on top
	_is.exposure = 400000;		//10ns increments
	_is.period = 500000;		//Frame period in 10ns increments
	_is.gain = sensor->getMinGain();
	_is.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&_is.geometry);
	_is.disableRingBuffer = 0;
	_is.mode = RECORD_MODE_NORMAL;
	_is.prerecordFrames = 1;
	_is.segmentLengthFrames = imagerSettings.recRegionSizeFrames;
	_is.segments = 1;
	_is.temporary = 1;

	retVal = setImagerSettings(_is);
	if(SUCCESS != retVal) {
		qDebug("autoColGainCorrection: Error during setImagerSettings %d", retVal);
		return retVal;
	}

	retVal = adjustExposureToValue(CAMERA_MAX_EXPOSURE_TARGET, 100, false);
	if(SUCCESS != retVal) {
		qDebug("autoColGainCorrection: Error during adjustExposureToValue %d", retVal);
		return retVal;
	}

	retVal = computeColGainCorrection(1, true);
	if(SUCCESS != retVal)
		return retVal;

	_is.gain = LUX1310_GAIN_4;
	retVal = setImagerSettings(_is);
	if(SUCCESS != retVal) {
		qDebug("autoColGainCorrection: Error during setImagerSettings(2) %d", retVal);
		return retVal;
	}

	retVal = adjustExposureToValue(CAMERA_MAX_EXPOSURE_TARGET, 100, false);
	if(SUCCESS != retVal) {
		qDebug("autoColGainCorrection: Error during adjustExposureToValue(2) %d", retVal);
		return retVal;
	}

	retVal = computeColGainCorrection(1, true);
	if(SUCCESS != retVal)
		return retVal;
#else
	struct timespec tRefresh;
	Int32 retVal;

	retVal = setRecSequencerModeCalLoop();
	if (SUCCESS != retVal) {
		return retVal;
	}

	//Turn off calibration light
	io->setOutLevel(0);

	/* Activate the recording sequencer. */
	startSequencer();
	ui->setRecLEDFront(true);
	ui->setRecLEDBack(true);

	sensor->adcOffsetTraining(&imagerSettings.geometry, CAL_REGION_START, CAL_REGION_FRAMES);

	//Turn on calibration light
	io->setOutLevel((1 << 1));
	factoryGainColumns(&imagerSettings.geometry, CAL_REGION_START, &tRefresh);

	terminateRecord();
	ui->setRecLEDFront(false);
	ui->setRecLEDBack(false);
#endif
	return SUCCESS;
}

//Will only decrease exposure, fails if the current set exposure is not clipped
Int32 Camera::adjustExposureToValue(UInt32 level, UInt32 tolerance, bool includeFPNCorrection)
{
	Int32 retVal;
	ImagerSettings_t _is = getImagerSettings();
	UInt32 val;
	UInt32 iterationCount = 0;
	const UInt32 iterationCountMax = 32;

	//Repeat recording frames and halving the exposure until the pixel value is below the desired level
	do {
		retVal = recordFrames(1);
		if(SUCCESS != retVal)
			return retVal;

		val = getMiddlePixelValue(includeFPNCorrection);
		qDebug() << "Got val of" << val;

		//If the pixel value is above the limit, halve the exposure
		if(val > level)
		{
			qDebug() << "Reducing exposure";
			_is.exposure /= 2;
			retVal = setImagerSettings(_is);
			if(SUCCESS != retVal)
				return retVal;
		}
		iterationCount++;
	} while (val > level && iterationCount <= iterationCountMax);

	if(iterationCount > iterationCountMax)
		return CAMERA_ITERATION_LIMIT_EXCEEDED;

	//If we only went through one iteration, the signal wasn't clipped, return failure
	if(1 == iterationCount)
		return CAMERA_LOW_SIGNAL_ERROR;

	//At this point the exposure is set so that the pixel is at or below the desired level
	iterationCount = 0;
	do
	{
		double ratio = (double)level / (double)val;
		qDebug() << "Ratio is "<< ratio << "level:" << level << "val:" << val << "oldExp:" << _is.exposure;
		//Scale exposure by the ratio, this should produce the correct exposure value to get the desired level
		_is.exposure = (UInt32)((double)_is.exposure * ratio);
		qDebug() << "newExp:" << _is.exposure;
		retVal = setImagerSettings(_is);
		if(SUCCESS != retVal)
			return retVal;

		//Check the value was correct
		retVal = recordFrames(1);
		if(SUCCESS != retVal)
			return retVal;

		val = getMiddlePixelValue(includeFPNCorrection);
		iterationCount++;
	} while(abs(val - level) > tolerance && iterationCount <= iterationCountMax);
	qDebug() << "Final exposure set to" << ((double)_is.exposure / 100.0) << "us, final pixel value" << val << "expected pixel value" << level;

	if(iterationCount > iterationCountMax)
		return CAMERA_ITERATION_LIMIT_EXCEEDED;

	return SUCCESS;
}


Int32 Camera::recordFrames(UInt32 numframes)
{
	Int32 retVal;
	UInt32 count;
	const int countMax = 500;
	int ms = 1;
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };

	qDebug() << "Starting record of one frame";

	CameraRecordModeType oldMode = imagerSettings.mode;
	imagerSettings.mode = RECORD_MODE_FPN;

	retVal = setRecSequencerModeSingleBlock(numframes + 1);
	if(SUCCESS != retVal)
		return retVal;

	retVal = startRecording();
	if(SUCCESS != retVal)
		return retVal;

	for(count = 0; (count < countMax) && recording; count++) {nanosleep(&ts, NULL);}

	if(count == countMax)	//If after the timeout recording hasn't finished
	{
		qDebug() << "Error: Record failed to stop within timeout period.";

		retVal = stopRecording();
		if(SUCCESS != retVal)
			qDebug() << "Error: Stop Record failed";

		return CAMERA_RECORD_FRAME_ERROR;
	}

	qDebug() << "Record done";

	imagerSettings.mode = oldMode;

	return SUCCESS;
}

UInt32 Camera::getMiddlePixelValue(bool includeFPNCorrection)
{
	//gpmc->write32(GPMC_PAGE_OFFSET_ADDR, REC_REGION_START);	//Read from the beginning of the record region

	UInt32 quadStartX = getImagerSettings().geometry.hRes / 2 & 0xFFFFFFFE;
	UInt32 quadStartY = getImagerSettings().geometry.vRes / 2 & 0xFFFFFFFE;
	//3=G B
	//  R G

	UInt16 bRaw = gpmc->readPixel12((quadStartY + 1) * imagerSettings.geometry.hRes + quadStartX, REC_REGION_START * BYTES_PER_WORD);
	UInt16 gRaw = gpmc->readPixel12(quadStartY * imagerSettings.geometry.hRes + quadStartX, REC_REGION_START * BYTES_PER_WORD);
	UInt16 rRaw = gpmc->readPixel12(quadStartY * imagerSettings.geometry.hRes + quadStartX + 1, REC_REGION_START * BYTES_PER_WORD);

	if(includeFPNCorrection)
	{
		bRaw -= gpmc->readPixel12((quadStartY + 1) * imagerSettings.geometry.hRes + quadStartX, FPN_ADDRESS * BYTES_PER_WORD);
		gRaw -= gpmc->readPixel12(quadStartY * imagerSettings.geometry.hRes + quadStartX, FPN_ADDRESS * BYTES_PER_WORD);
		rRaw -= gpmc->readPixel12(quadStartY * imagerSettings.geometry.hRes + quadStartX + 1, FPN_ADDRESS * BYTES_PER_WORD);

		//If the result underflowed, clip it to zero
		if(bRaw >= (1 << SENSOR_DATA_WIDTH)) bRaw = 0;
		if(gRaw >= (1 << SENSOR_DATA_WIDTH)) gRaw = 0;
		if(rRaw >= (1 << SENSOR_DATA_WIDTH)) rRaw = 0;
	}

	qDebug() << "RGB values read:" << rRaw << gRaw << bRaw;
	int maxVal = max(rRaw, max(gRaw, bRaw));

	//gpmc->write32(GPMC_PAGE_OFFSET_ADDR, 0);
	return maxVal;
}

//frameBuffer must be a UInt16 array with enough elements to to hold all pixels at the current recording resolution
Int32 Camera::getRawCorrectedFrame(UInt32 frame, UInt16 * frameBuffer)
{
	Int32 retVal;
	double gainCorrection[16];
	UInt32 pixelsPerFrame = recordingData.is.geometry.pixels();

	retVal = readDCG(gainCorrection);
	if(SUCCESS != retVal)
		return retVal;

	UInt16 * fpnUnpacked = new UInt16[pixelsPerFrame];
	if(NULL == fpnUnpacked)
		return CAMERA_MEM_ERROR;

	retVal = readFPN(fpnUnpacked);
	if(SUCCESS != retVal)
	{
		delete fpnUnpacked;
		return retVal;
	}

	retVal = readCorrectedFrame(frame, frameBuffer, fpnUnpacked, gainCorrection);
	if(SUCCESS != retVal)
	{
		delete fpnUnpacked;
		return retVal;
	}

	delete fpnUnpacked;

	return SUCCESS;
}

//frameBuffer must be a UInt16 array with enough elements to to hold all pixels at the current recording resolution
Int32 Camera::getRawCorrectedFramesAveraged(UInt32 frame, UInt32 framesToAverage, UInt16 * frameBuffer)
{
	Int32 retVal;
	double gainCorrection[16];
	UInt32 pixelsPerFrame = recordingData.is.geometry.pixels();

	retVal = readDCG(gainCorrection);
	if(SUCCESS != retVal)
		return retVal;

	UInt16 * fpnUnpacked = new UInt16[pixelsPerFrame];
	if(NULL == fpnUnpacked)
		return CAMERA_MEM_ERROR;

	retVal = readFPN(fpnUnpacked);
	if(SUCCESS != retVal)
	{
		delete fpnUnpacked;
		return retVal;
	}

	//Allocate memory for sum buffer
	UInt32 * summingBuffer = new UInt32[pixelsPerFrame];

	if(NULL == summingBuffer)
		return CAMERA_MEM_ERROR;

	//Zero sum buffer
	for(int i = 0; i < pixelsPerFrame; i++)
		summingBuffer[i] = 0;

	//For each frame to average
	for(int i = 0; i < framesToAverage; i++)
	{
		//Read in the frame
		retVal = readCorrectedFrame(frame + i, frameBuffer, fpnUnpacked, gainCorrection);
		if(SUCCESS != retVal)
		{
			delete fpnUnpacked;
			delete summingBuffer;
			return retVal;
		}

		//Add pixels to sum buffer
		for(int j = 0; j < pixelsPerFrame; j++)
			summingBuffer[j] += frameBuffer[j];

	}

	//Divide to get average and put in result buffer
	for(int i = 0; i < pixelsPerFrame; i++)
		frameBuffer[i] = summingBuffer[i] / framesToAverage;

	delete fpnUnpacked;
	delete summingBuffer;

	return SUCCESS;
}

Int32 Camera::readDCG(double * gainCorrection)
{
	size_t count = 0;
	QString filename;
	QFile fp;

	//Generate the filename for this particular resolution and offset
	if(imagerSettings.gain >= LUX1310_GAIN_4)
		filename.sprintf("cal:dcgH.bin");
	else
		filename.sprintf("cal:dcgL.bin");

	QFileInfo colGainFile(filename);
	if (colGainFile.exists() && colGainFile.isFile()) {
		qDebug("Found colGain file, %s", colGainFile.absoluteFilePath().toLocal8Bit().constData());

		fp.setFileName(filename);
		fp.open(QIODevice::ReadOnly);
		if(!fp.isOpen()) {
			qDebug("Error: File couldn't be opened");
		}

		//If the column gain file exists, read it in
		count = fp.read((char*) gainCorrection, sizeof(gainCorrection[0]) * LUX1310_HRES_INCREMENT);
		fp.close();
		if (count < sizeof(gainCorrection[0]) * LUX1310_HRES_INCREMENT)
			return CAMERA_FILE_ERROR;
	}
	else {
		return CAMERA_FILE_ERROR;
	}

	//If the file wasn't fully read in, fail
	if(LUX1310_HRES_INCREMENT != count)
		return CAMERA_FILE_ERROR;

	return SUCCESS;
}

Int32 Camera::readFPN(UInt16 * fpnUnpacked)
{
	UInt32 pixelsPerFrame = recordingData.is.geometry.pixels();
	UInt32 bytesPerFrame = recordingData.is.geometry.size();
	UInt32 * fpnBuffer32 = new UInt32[bytesPerFrame / 4];
	UInt8 * fpnBuffer = (UInt8 *)fpnBuffer32;

	if(NULL == fpnBuffer32)
		return CAMERA_MEM_ERROR;

	//Read the FPN data in
	gpmc->readAcqMem(fpnBuffer32, FPN_ADDRESS, bytesPerFrame);

	//Unpack the FPN data
	for(int i = 0; i < pixelsPerFrame; i++)
	{
		fpnUnpacked[i] = readPixelBuf12(fpnBuffer, i);
	}

	delete fpnBuffer32;

	return SUCCESS;
}

Int32 Camera::readCorrectedFrame(UInt32 frame, UInt16 * frameBuffer, UInt16 * fpnInput, double * gainCorrection)
{
	UInt32 pixelsPerFrame = recordingData.is.geometry.pixels();
	UInt32 bytesPerFrame = recordingData.is.geometry.size();

	UInt32 * rawFrameBuffer32 = new UInt32[bytesPerFrame / 4];
	UInt8 * rawFrameBuffer = (UInt8 *)rawFrameBuffer32;
	UInt32 frameAddr = REC_REGION_START + frame * getFrameSizeWords(&recordingData.is.geometry);

	if(NULL == rawFrameBuffer32)
		return CAMERA_MEM_ERROR;

	//Read in the frame and FPN data
	//Get one frame into the raw buffer
	gpmc->readAcqMem(rawFrameBuffer32, frameAddr, bytesPerFrame);

	//Subtract the FPN data from the buffer
	for(int i = 0; i < pixelsPerFrame; i++)
	{
		frameBuffer[i] = readPixelBuf12(rawFrameBuffer, i) - fpnInput[i];

		//If the result underflowed, clip it to zero
		if(frameBuffer[i] >= (1 << SENSOR_DATA_WIDTH))
			frameBuffer[i] = 0;

		frameBuffer[i] = (UInt16)((double)frameBuffer[i] * gainCorrection[i % LUX1310_HRES_INCREMENT]);

		//If over max value, clip to max
		if(frameBuffer[i] > ((1 << SENSOR_DATA_WIDTH) - 1))
			frameBuffer[i] = ((1 << SENSOR_DATA_WIDTH) - 1);
	}

	delete rawFrameBuffer32;

	return SUCCESS;
}

void Camera::loadCCMFromSettings(void)
{
	QSettings appSettings;
	QString currentName = appSettings.value("colorMatrix/current", "").toString();

	/* Special case for the custom color matrix. */
	if (currentName == "Custom") {
		if (appSettings.beginReadArray("colorMatrix") >= 9) {
			for (int i = 0; i < 9; i++) {
				appSettings.setArrayIndex(i);
				colorCalMatrix[i] = appSettings.value("ccmValue").toDouble();
			}
			appSettings.endArray();
			return;
		}
		appSettings.endArray();
	}

	/* For preset matricies, look them up by name. */
	for (int i = 0; i < sizeof(ccmPresets)/sizeof(ccmPresets[0]); i++) {
		if (currentName == ccmPresets[i].name) {
			memcpy(colorCalMatrix, ccmPresets[i].matrix, sizeof(ccmPresets[0].matrix));
			return;
		}
	}

	/* Otherwise, use the default matrix. */
	memcpy(colorCalMatrix, ccmPresets[0].matrix, sizeof(ccmPresets[0].matrix));
}

#define COLOR_MATRIX_MAXVAL	((1 << SENSOR_DATA_WIDTH) * (1 << COLOR_MATRIX_INT_BITS))

void Camera::setCCMatrix(const double *matrix)
{
	int ccm[] = {
		/* First row */
		(int)(4096.0 * matrix[0]),
		(int)(4096.0 * matrix[1]),
		(int)(4096.0 * matrix[2]),
		/* Second row */
		(int)(4096.0 * matrix[3]),
		(int)(4096.0 * matrix[4]),
		(int)(4096.0 * matrix[5]),
		/* Third row */
		(int)(4096.0 * matrix[6]),
		(int)(4096.0 * matrix[7]),
		(int)(4096.0 * matrix[8])
	};

	/* Check if the colour matrix has clipped, and scale it back down if necessary. */
	int i;
	int peak = 4096;
	for (i = 0; i < 9; i++) {
		if (ccm[i] > peak) peak = ccm[i];
		if (-ccm[i] > peak) peak = -ccm[i];
	}
	if (peak > COLOR_MATRIX_MAXVAL) {
		fprintf(stderr, "Warning: Color matrix has clipped - scaling down to fit\n");
		for (i = 0; i < 9; i++) ccm[i] = (ccm[i] * COLOR_MATRIX_MAXVAL) / peak;
	}

	fprintf(stderr, "Setting Color Matrix:\n");
	fprintf(stderr, "\t%06f %06f %06f\n",   ccm[0] / 4096.0, ccm[1] / 4096.0, ccm[2] / 4096.0);
	fprintf(stderr, "\t%06f %06f %06f\n",   ccm[3] / 4096.0, ccm[4] / 4096.0, ccm[5] / 4096.0);
	fprintf(stderr, "\t%06f %06f %06f\n\n", ccm[6] / 4096.0, ccm[7] / 4096.0, ccm[8] / 4096.0);

	gpmc->write16(CCM_11_ADDR, ccm[0]);
	gpmc->write16(CCM_12_ADDR, ccm[1]);
	gpmc->write16(CCM_13_ADDR, ccm[2]);

	gpmc->write16(CCM_21_ADDR, ccm[3]);
	gpmc->write16(CCM_22_ADDR, ccm[4]);
	gpmc->write16(CCM_23_ADDR, ccm[5]);

	gpmc->write16(CCM_31_ADDR, ccm[6]);
	gpmc->write16(CCM_32_ADDR, ccm[7]);
	gpmc->write16(CCM_33_ADDR, ccm[8]);
}

void Camera::setWhiteBalance(const double *rgb)
{
	double r = rgb[0] * imgGain;
	double g = rgb[1] * imgGain;
	double b = rgb[2] * imgGain;

	/* If imgGain white balance to clip, then scale it back. */
	if ((r > 8.0) || (g > 8.0) || (b > 8.0)) {
		double wbMax = rgb[0];
		if (wbMax < rgb[1]) wbMax = rgb[1];
		if (wbMax < rgb[2]) wbMax = rgb[2];

		qWarning("White Balance clipped due to imgGain of %g", imgGain);
		r = rgb[0] * 8.0 / wbMax;
		g = rgb[1] * 8.0 / wbMax;
		b = rgb[2] * 8.0 / wbMax;
	}

	qDebug("Setting WB Matrix: %06f %06f %06f", r, g, b);

	gpmc->write16(WBAL_RED_ADDR,   (int)(4096.0 * r));
	gpmc->write16(WBAL_GREEN_ADDR, (int)(4096.0 * g));
	gpmc->write16(WBAL_BLUE_ADDR,  (int)(4096.0 * b));
}

Int32 Camera::autoWhiteBalance(UInt32 x, UInt32 y)
{
	UInt32 numChannels = sensor->getHResIncrement();
	const UInt32 min_sum = 100 * (numChannels * numChannels) / 2;

	UInt32 r_sum = 0;
	UInt32 g_sum = 0;
	UInt32 b_sum = 0;
	double scale;
	int i, j;

	/* Round x and y down to units of 16 pixels. */
	/* This only really needs to be rounded down to match the bayer pattern. */
	x &= 0xFFFFFFF0;
	y &= 0xFFFFFFF0;

	for (i = 0; i < numChannels; i += 2) {
		/* Even Rows - Green/Red Pixels */
		for (j = 0; j < numChannels; j++) {
			UInt16 pix = readPixelCal(x + j, y + i, LIVE_REGION_START, &imagerSettings.geometry);
			if (((pix + 128) >> imagerSettings.geometry.bitDepth) != 0) {
				return CAMERA_CLIPPED_ERROR;
			}
			if (j & 1) {
				r_sum += pix * 2;
			} else {
				g_sum += pix;
			}
		}

		/* Odd Rows - Blue/Green Pixels */
		for (j = 0; j < numChannels; j++) {
			UInt16 pix = readPixelCal(x + j, y + i + 1, LIVE_REGION_START, &imagerSettings.geometry);
			if (((pix + 128) >> imagerSettings.geometry.bitDepth) != 0) {
				return CAMERA_CLIPPED_ERROR;
			}
			if (j & 1) {
				g_sum += pix;
			} else {
				b_sum += pix * 2;
			}
		}
	}

	qDebug("WhiteBalance RGB average: %d %d %d",
			(2*r_sum) / (numChannels * numChannels),
			(2*g_sum) / (numChannels * numChannels),
			(2*b_sum) / (numChannels * numChannels));

	if ((r_sum < min_sum) || (g_sum < min_sum) || (b_sum < min_sum))
		return CAMERA_LOW_SIGNAL_ERROR;

	/* Find the highest channel (probably green) */
	scale = g_sum;
	if (scale < r_sum) scale = r_sum;
	if (scale < b_sum) scale = b_sum;

	whiteBalMatrix[0] = scale / r_sum;
	whiteBalMatrix[1] = scale / g_sum;
	whiteBalMatrix[2] = scale / b_sum;

	setWhiteBalance(whiteBalMatrix);
	return SUCCESS;
}

UInt8 Camera::getWBIndex(){
	QSettings appsettings;
	return appsettings.value("camera/WBIndex", 2).toUInt();
}

void Camera::setWBIndex(UInt8 index){
	QSettings appsettings;
	appsettings.setValue("camera/WBIndex", index);
}


void Camera::setFocusAid(bool enable)
{
	UInt32 startX, startY, cropX, cropY;

	if(enable)
	{
		//Set crop window to native resolution of LCD or unchanged if we're scaling up
		if(imagerSettings.geometry.hRes > 600 || imagerSettings.geometry.vRes > 480)
		{
			//Depending on aspect ratio, set the display window appropriately
			if((imagerSettings.geometry.vRes * MAX_FRAME_SIZE_H) > (imagerSettings.geometry.hRes * MAX_FRAME_SIZE_V))	//If it's taller than the display aspect
			{
				cropY = 480;
				cropX = cropY * imagerSettings.geometry.hRes / imagerSettings.geometry.vRes;
				if(cropX & 1)	//If it's odd, round it up to be even
					cropX++;
				startX = (imagerSettings.geometry.hRes - cropX) / 2;
				startY = (imagerSettings.geometry.vRes - cropY) / 2;

			}
			else
			{
				cropX = 600;
				cropY = cropX * imagerSettings.geometry.vRes / imagerSettings.geometry.hRes;
				if(cropY & 1)	//If it's odd, round it up to be even
					cropY++;
				startX = (imagerSettings.geometry.hRes - cropX) / 2;
				startY = (imagerSettings.geometry.vRes - cropY) / 2;

			}
			qDebug() << "Setting startX" << startX << "startY" << startY << "cropX" << cropX << "cropY" << cropY;
			vinst->setScaling(startX & 0xFFFF8, startY, cropX, cropY);	//StartX must be a multiple of 8
		}
	}
	else
	{
		vinst->setScaling(0, 0, imagerSettings.geometry.hRes, imagerSettings.geometry.vRes);
	}
}

bool Camera::getFocusAid()
{
	/* FIXME: Not implemented */
	return false;
}

/* Nearest multiple rounding */
static inline UInt32
round(UInt32  x, UInt32 mult)
{
	UInt32 offset = (x) % (mult);
	return (offset >= mult/2) ? x - offset + mult : x - offset;
}

Int32 Camera::blackCalAllStdRes(bool factory, QProgressDialog *dialog)
{
	int g;
	ImagerSettings_t settings;
	FrameGeometry	maxSize = sensor->getMaxGeometry();

	//Populate the common resolution combo box from the list of resolutions
	QFile fp;
	UInt32 retVal = SUCCESS;
	QStringList resolutions;
	QString filename;
	QString line;

	filename.append("camApp:resolutions");
	QFileInfo resolutionsFile(filename);
	if (resolutionsFile.exists() && resolutionsFile.isFile()) {
		fp.setFileName(filename);
		fp.open(QIODevice::ReadOnly);
		if(!fp.isOpen()) {
			qDebug("Error: resolutions file couldn't be opened");
			return CAMERA_FILE_ERROR;
		}
	}
	else {
		qDebug("Error: resolutions file isn't present");
		return CAMERA_FILE_ERROR;
	}

	/* Read the resolutions file into a list. */
	while (true) {
		QString tmp = fp.readLine(30);
		QStringList strlist;
		FrameGeometry size;

		/* Try to read another resolution from the file. */
		if (tmp.isEmpty() || tmp.isNull()) break;
		strlist = tmp.split('x');
		if (strlist.count() < 2) break;

		/* If it's a supported resolution, add it to the list. */
		size.hRes = strlist[0].toInt(); //pixels
		size.vRes = strlist[1].toInt(); //pixels
		size.vDarkRows = 0;	//dark rows
		size.bitDepth = maxSize.bitDepth;
		size.hOffset = round((maxSize.hRes - size.hRes) / 2, sensor->getHResIncrement());
		size.vOffset = round((maxSize.vRes - size.vRes) / 2, sensor->getVResIncrement());
		if (sensor->isValidResolution(&size)) {
			line.sprintf("%ux%u", size.hRes, size.vRes);
			resolutions.append(line);
		}
	}
	fp.close();

	/* Ensure that the maximum sensor size is also included. */
	line.sprintf("%ux%u", maxSize.hRes, maxSize.vRes);
	if (!resolutions.contains(line)) {
		resolutions.prepend(line);
	}

	/* If we have a progress dialog - figure out how many calibration steps to do. */
	if (dialog) {
		int maxcals = 0;

		/* Count up the number of gain settings to calibrate for. */
		for (g = sensor->getMinGain(); g <= sensor->getMaxGain(); g *= 2) {
			 maxcals += resolutions.count();
		}

		dialog->setMaximum(maxcals);
		dialog->setValue(0);
		dialog->setAutoClose(false);
		dialog->setAutoReset(false);
	}

	/* Disable the video port during calibration. */
	vinst->pauseDisplay();

	//For each gain
	int progress = 0;
	for(g = sensor->getMinGain(); g <= sensor->getMaxGain(); g *= 2)
	{
		// Configure for maximum resolution
		memcpy(&settings.geometry, &maxSize, sizeof(FrameGeometry));
		settings.geometry.hRes = maxSize.hRes;
		settings.geometry.vRes = maxSize.vRes;
		settings.geometry.vDarkRows = 0;
		settings.geometry.bitDepth = maxSize.bitDepth;
		settings.geometry.hOffset = 0;
		settings.geometry.vOffset = 0;
		settings.gain = g;
		settings.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&settings.geometry);
		settings.period = sensor->getMinFramePeriod(&settings.geometry);
		settings.exposure = sensor->getMaxIntegrationTime(settings.period, &settings.geometry);
		settings.disableRingBuffer = 0;
		settings.mode = RECORD_MODE_NORMAL;
		settings.prerecordFrames = 1;
		settings.segmentLengthFrames = imagerSettings.recRegionSizeFrames;
		settings.segments = 1;
		settings.temporary = 0;
		retVal = setImagerSettings(settings);
		if (SUCCESS != retVal)
			return retVal;

		/* Perform gain calibraton. */
		retVal = autoGainCalibration();
		if (SUCCESS != retVal)
			return retVal;

		QStringListIterator iter(resolutions);
		while (iter.hasNext()) {
			QString value = iter.next();
			QStringList tmp = value.split('x');

			// Split the resolution string on 'x' into horizontal and vertical sizes.
			settings.geometry.hRes = tmp[0].toInt(); //pixels
			settings.geometry.vRes = tmp[1].toInt(); //pixels
			settings.geometry.vDarkRows = 0;	//dark rows
			settings.geometry.bitDepth = maxSize.bitDepth;
			settings.geometry.hOffset = round((maxSize.hRes - settings.geometry.hRes) / 2, sensor->getHResIncrement());
			settings.geometry.vOffset = round((maxSize.vRes - settings.geometry.vRes) / 2, sensor->getVResIncrement());
			settings.gain = g;
			settings.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&settings.geometry);
			settings.period = sensor->getMinFramePeriod(&settings.geometry);
			settings.exposure = sensor->getMaxIntegrationTime(settings.period, &settings.geometry);
			settings.disableRingBuffer = 0;
			settings.mode = RECORD_MODE_NORMAL;
			settings.prerecordFrames = 1;
			settings.segmentLengthFrames = imagerSettings.recRegionSizeFrames;
			settings.segments = 1;
			settings.temporary = 0;

			/* Update the progress dialog. */
			progress++;
			if (dialog) {
				QString label;
				if (dialog->wasCanceled()) {
					goto exit_calibration;
				}
				label.sprintf("Computing calibration for %ux%u at x%d gain",
							  settings.geometry.hRes, settings.geometry.vRes, settings.gain);
				dialog->setValue(progress);
				dialog->setLabelText(label);
				QCoreApplication::processEvents();
			}

			retVal = setImagerSettings(settings);
			if (SUCCESS != retVal)
				return retVal;

			qDebug("Doing offset correction for %ux%u...", settings.geometry.hRes, settings.geometry.vRes);
			retVal = autoOffsetCalibration();
			if(SUCCESS != retVal)
				return retVal;

			qDebug("Doing FPN correction for %ux%u...", settings.geometry.hRes, settings.geometry.vRes);
			retVal = autoFPNCorrection(16, true, false, factory);	//Factory mode
			if(SUCCESS != retVal)
				return retVal;

			qDebug() << "Done.";
		}
	}
exit_calibration:

	fp.close();

	memcpy(&settings.geometry, &maxSize, sizeof(maxSize));
	settings.geometry.vDarkRows = 0; //Inactive dark rows on top
	settings.gain = sensor->getMinGain();
	settings.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&settings.geometry);
	settings.period = sensor->getMinFramePeriod(&settings.geometry);
	settings.exposure = sensor->getMaxIntegrationTime(settings.period, &settings.geometry);

	retVal = setImagerSettings(settings);
	vinst->liveDisplay((sensor->getSensorQuirks() & SENSOR_QUIRK_UPSIDE_DOWN) != 0);
	if(SUCCESS != retVal) {
		qDebug("blackCalAllStdRes: Error during setImagerSettings %d", retVal);
		return retVal;
	}

	retVal = loadFPNFromFile();
	if(SUCCESS != retVal) {
		qDebug("blackCalAllStdRes: Error during loadFPNFromFile %d", retVal);
		return retVal;
	}

	return SUCCESS;
}

Int32 Camera::takeWhiteReferences(void)
{
	Int32 retVal = SUCCESS;
	ImagerSettings_t _is;
	UInt32 g;
	QFile fp;
	QString filename;
	double exposures[] = {0.000244141,
						  0.000488281,
						  0.000976563,
						  0.001953125,
						  0.00390625,
						  0.0078125,
						  0.015625,
						  0.03125,
						  0.0625,
						  0.125,
						  0.25,
						  0.5,
						  1};

	_is.geometry = sensor->getMaxGeometry();
	_is.geometry.vDarkRows = 0;	//Inactive dark rows on top
	_is.exposure = 400000;		//10ns increments
	_is.period = 500000;		//Frame period in 10ns increments
	_is.recRegionSizeFrames = getMaxRecordRegionSizeFrames(&_is.geometry);
	_is.disableRingBuffer = 0;
	_is.mode = RECORD_MODE_NORMAL;
	_is.prerecordFrames = 1;
	_is.segmentLengthFrames = imagerSettings.recRegionSizeFrames;
	_is.segments = 1;
	_is.temporary = 0;

	UInt32 pixelsPerFrame = _is.geometry.pixels();

	UInt16 * frameBuffer = new UInt16[pixelsPerFrame];
	if(NULL == frameBuffer)
		return CAMERA_MEM_ERROR;


	//For each gain
	for(g = sensor->getMinGain(); g <= sensor->getMaxGain(); g *= 2)
	{
		UInt32 nomExp;

		//Set gain then adjust expsure to get initial exposure value
		_is.gain = g;


		retVal = setImagerSettings(_is);
		if(SUCCESS != retVal)
			goto cleanupTakeWhiteReferences;

		retVal = adjustExposureToValue(4096*3/4);
		if(SUCCESS != retVal)
			goto cleanupTakeWhiteReferences;

		nomExp = imagerSettings.exposure;

		//For each exposure value
		for(int i = 0; i < (sizeof(exposures)/sizeof(exposures[0])); i++)
		{
			//Set exposure
			_is.exposure = (UInt32)((double)nomExp * exposures[i]);

			retVal = setImagerSettings(_is);
			if(SUCCESS != retVal)
			{
				delete frameBuffer;
				return retVal;
			}

			qDebug("Recording frames for gain G%u and exposure %d", g, i);
			//Record frames
			retVal = recordFrames(16);
			if(SUCCESS != retVal)
				goto cleanupTakeWhiteReferences;

			//Get the frames averaged and save to file
			qDebug() << "Doing getRawCorrectedFramesAveraged()";
			retVal = getRawCorrectedFramesAveraged(0, 16, frameBuffer);
			if(SUCCESS != retVal)
				goto cleanupTakeWhiteReferences;


			//Generate the filename for this particular resolution and offset
			filename.sprintf("userFPN/wref_G%u_LV%d.raw", g, i+1);

			qDebug("Writing WhiteReference to file %s", filename.toUtf8().data());

			fp.setFileName(filename);
			fp.open(QIODevice::WriteOnly);
			if(!fp.isOpen()) {
				qDebug() << "Error: File couldn't be opened";
				retVal = CAMERA_FILE_ERROR;
				goto cleanupTakeWhiteReferences;
			}

			retVal = fp.write((const char*)frameBuffer, sizeof(frameBuffer[0])*pixelsPerFrame);
			if (retVal != (sizeof(frameBuffer[0])*pixelsPerFrame)) {
				qDebug("Error writing WhiteReference data to file: %s", fp.errorString().toUtf8().data());
			}
			fp.flush();
			fp.close();
		}
	}

cleanupTakeWhiteReferences:
	delete frameBuffer;
	return retVal;
}

bool Camera::getButtonsOnLeft(void){
	QSettings appSettings;
	return (appSettings.value("camera/ButtonsOnLeft", false).toBool());
}

void Camera::setButtonsOnLeft(bool en){
	QSettings appSettings;
	ButtonsOnLeft = en;
	appSettings.setValue("camera/ButtonsOnLeft", en);
}

bool Camera::getUpsideDownDisplay(){
	QSettings appSettings;
	return (appSettings.value("camera/UpsideDownDisplay", false).toBool());
}

void Camera::setUpsideDownDisplay(bool en){
	QSettings appSettings;
	UpsideDownDisplay = en;
	appSettings.setValue("camera/UpsideDownDisplay", en);
}

bool Camera::RotationArgumentIsSet()
{
	QScreen *qscreen = QScreen::instance();
	return qscreen->classId() == QScreen::TransformedClass;
}


void Camera::upsideDownTransform(int rotation){
	QWSDisplay::setTransformation(rotation);
}

bool Camera::getFocusPeakEnable(void)
{
	QSettings appSettings;
	return appSettings.value("camera/focusPeak", false).toBool();
}

void Camera::setFocusPeakEnable(bool en)
{
	QSettings appSettings;
	appSettings.setValue("camera/focusPeak", en);
	vinst->setDisplayOptions(getZebraEnable(), en ? (FocusPeakColors)getFocusPeakColor() : FOCUS_PEAK_DISABLE);
}

int Camera::getFocusPeakColor(void)
{
	QSettings appSettings;
	return appSettings.value("camera/focusPeakColor", FOCUS_PEAK_CYAN).toInt();
}

void Camera::setFocusPeakColor(int value)
{
	QSettings appSettings;
	appSettings.setValue("camera/focusPeakColor", value);
	vinst->setDisplayOptions(getZebraEnable(), getFocusPeakEnable() ? (FocusPeakColors)value : FOCUS_PEAK_DISABLE);
}

bool Camera::getZebraEnable(void)
{
	QSettings appSettings;
	return appSettings.value("camera/zebra", true).toBool();
}

void Camera::setZebraEnable(bool en)
{
	QSettings appSettings;
	appSettings.setValue("camera/zebra", en);
	vinst->setDisplayOptions(en, getFocusPeakEnable() ? (FocusPeakColors)getFocusPeakColor() : FOCUS_PEAK_DISABLE);
}

int Camera::getUnsavedWarnEnable(void){
	QSettings appSettings;
	return appSettings.value("camera/unsavedWarn", 1).toInt();
	//If there is unsaved video in RAM, prompt to start record.  2=always, 1=if not reviewed, 0=never
}

void Camera::setUnsavedWarnEnable(int newSetting){
	QSettings appSettings;
	unsavedWarnEnabled = newSetting;
	appSettings.setValue("camera/unsavedWarn", newSetting);
}


void Camera::set_autoSave(bool state) {
	QSettings appSettings;
	autoSave = state;
	appSettings.setValue("camera/autoSave", state);
}

bool Camera::get_autoSave() {
	QSettings appSettings;
	return appSettings.value("camera/autoSave", false).toBool();
}


void Camera::set_autoRecord(bool state) {
	QSettings appSettings;
	autoRecord = state;
	appSettings.setValue("camera/autoRecord", state);
}

bool Camera::get_autoRecord() {
	QSettings appSettings;
	return appSettings.value("camera/autoRecord", false).toBool();
}


void Camera::set_demoMode(bool state) {
	QSettings appSettings;
	demoMode = state;
	appSettings.setValue("camera/demoMode", state);
}

bool Camera::get_demoMode() {
	QSettings appSettings;
	return appSettings.value("camera/demoMode", false).toBool();
}

void* recDataThread(void *arg)
{
	Camera * cInst = (Camera *)arg;
	int ms = 100;
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
	bool recording;

	while(!cInst->terminateRecDataThread) {
		//On the falling edge of recording, call the user callback
		recording = cInst->getRecording();
		if(!recording && (cInst->lastRecording || cInst->recording))	//Take care of situtation where recording goes low->high-low between two interrutps by checking the cInst->recording flag
		{
			recording = false;
			cInst->ui->setRecLEDFront(false);
			cInst->ui->setRecLEDBack(false);
			cInst->recording = false;
		}
		cInst->lastRecording = recording;

		nanosleep(&ts, NULL);
	}

	pthread_exit(NULL);
}
