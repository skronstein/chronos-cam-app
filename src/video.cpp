#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <QDebug>
#include <memory.h>
#include <getopt.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <fcntl.h>
#include <poll.h>
#include "video.h"
#include "camera.h"
#include "util.h"

void catch_sigchild(int sig) { /* nop */ }

/* Check for the PID of the video daemon. */
void Video::checkpid(void)
{
	FILE *fp = popen("pidof cam-pipeline", "r");
	char line[64];
	if (fp == NULL) {
		return;
	}
	if (fgets(line, sizeof(line), fp) != NULL) {
		pid = strtol(line, NULL, 10);

	}
}

CameraErrortype Video::setScaling(UInt32 startX, UInt32 startY, UInt32 cropX, UInt32 cropY)
{
	/* TODO: Replace with a SIGUSR to change focus aid scaling. */
	return SUCCESS;
}

static VideoState parseVideoState(const QVariantMap &args)
{
	if (args["filesave"].toBool()) {
		return VIDEO_STATE_FILESAVE;
	}
	else if (args["playback"].toBool()) {
		return VIDEO_STATE_PLAYBACK;
	}
	else {
		return VIDEO_STATE_LIVEDISPLAY;
	}
}

static VideoStatus *parseVideoStatus(const QVariantMap &args, VideoStatus *st)
{
	st->state = parseVideoState(args);
	st->totalFrames = args["totalFrames"].toUInt();
	st->position = args["position"].toInt();
	st->totalSegments = args["totalSegments"].toUInt();
	st->segment = args["segment"].toUInt();
	st->framerate = args["framerate"].toDouble();
	return st;
}

VideoState Video::getStatus(VideoStatus *st)
{
	QDBusPendingReply<QVariantMap> reply;
	QVariantMap map;

	if (st) memset(st, 0, sizeof(VideoState));

	pthread_mutex_lock(&mutex);
	reply = iface.status();
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);
	if (reply.isError()) {
		/* TODO: Error handling */
		return VIDEO_STATE_LIVEDISPLAY;
	}

	map = reply.value();
	if (!st) return parseVideoState(map);

	parseVideoStatus(map, st);
	return st->state;
}

UInt32 Video::getPosition(void)
{
	QDBusPendingReply<QVariantMap> reply;
	QVariantMap map;

	pthread_mutex_lock(&mutex);
	reply = iface.status();
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);
	if (reply.isError()) {
		return 0;
	}
	map = reply.value();
	return map["position"].toUInt();
}

void Video::setPosition(unsigned int position)
{
	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;
	args.insert("framerate", QVariant(0));
	args.insert("position", QVariant(position));

	pthread_mutex_lock(&mutex);
	reply = iface.playback(args);
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		fprintf(stderr, "Failed to set playback position: %s - %s\n", err.name().data(), err.message().toAscii().data());
	}
}

void Video::setPlayback(int rate)
{
	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;
	args.insert("framerate", QVariant(rate));

	pthread_mutex_lock(&mutex);
	reply = iface.playback(args);
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		fprintf(stderr, "Failed to set playback rate: %s - %s\n", err.name().data(), err.message().toAscii().data());
	}
}


void Video::seekFrame(int delta)
{
	if (delta && (pid > 0)) {
		union sigval val = { .sival_int = delta };
		sigqueue(pid, SIGUSR1, val);
	}
}

void Video::loopPlayback(unsigned int start, unsigned int length, int rate)
{
	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;
	args.insert("framerate", QVariant(rate));
	args.insert("position", QVariant(start));
	args.insert("loopcount", QVariant(length));

	pthread_mutex_lock(&mutex);
	reply = iface.playback(args);
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		fprintf(stderr, "Failed to start playback loop: %s - %s\n", err.name().data(), err.message().toAscii().data());
	}
}

void Video::setDisplayOptions(bool zebra, FocusPeakColors fpColor)
{
	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;
	args.insert("zebra", QVariant(zebra));
	args.insert("peaking", QVariant(fpColor));

	pthread_mutex_lock(&mutex);
	reply = iface.configure(args);
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		fprintf(stderr, "Failed to configure display options: %s - %s\n", err.name().data(), err.message().toAscii().data());
	}
}

void Video::liveDisplay(bool flip)
{
	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;
	args.insert("flip", QVariant(flip));

	pthread_mutex_lock(&mutex);
	reply = iface.livedisplay(args);
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);
}

void Video::pauseDisplay(void)
{
	QDBusPendingReply<QVariantMap> reply;

	pthread_mutex_lock(&mutex);
	reply = iface.pause();
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);
}

void Video::setOverlay(const char *format)
{
	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;

	args.insert("format", QVariant(format));
	args.insert("position", QVariant("bottom"));
	args.insert("textbox", QVariant("0x0"));

	pthread_mutex_lock(&mutex);
	reply = iface.overlay(args);
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		fprintf(stderr, "Failed to configure video overlay: %s - %s\n", err.name().data(), err.message().toAscii().data());
	}
	QSettings appSettings;
	appSettings.setValue("overlayEnabled", true);
}

bool Video::getOverlayStatus(){
	QSettings appSettings;
	return appSettings.value("overlayEnabled", false).toBool();
}

void Video::clearOverlay(void)
{
 	QVariantMap args;
	args.insert("format", QVariant(""));

	pthread_mutex_lock(&mutex);
	iface.overlay(args);
	pthread_mutex_unlock(&mutex);
	QSettings appSettings;
	appSettings.setValue("overlayEnabled", false);
}

void Video::flushRegions(void)
{
	QDBusPendingReply<QVariantMap> reply;

	pthread_mutex_lock(&mutex);
	reply = iface.flush();
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);
}

int Video::mkfilename(char *path, save_mode_type save_mode)
{
	char fname[1000];

	if(strlen(fileDirectory) == 0)
		return RECORD_NO_DIRECTORY_SET;

	strcpy(path, fileDirectory);
	if(strlen(filename) == 0)
	{
		//Fill timeinfo structure with the current time
		time_t rawtime;
		struct tm * timeinfo;

		time (&rawtime);
		timeinfo = localtime (&rawtime);

		sprintf(fname, "/vid_%04d-%02d-%02d_%02d-%02d-%02d",
					timeinfo->tm_year + 1900,
					timeinfo->tm_mon + 1,
					timeinfo->tm_mday,
					timeinfo->tm_hour,
					timeinfo->tm_min,
					timeinfo->tm_sec);
		strcat(path, fname);
	}
	else
	{
		strcat(path, "/");
		strcat(path, filename);
	}

	switch(save_mode) {
	case SAVE_MODE_H264:
		strcat(path, ".mp4");
		break;
	case SAVE_MODE_RAW16:
	case SAVE_MODE_RAW12:
		strcat(path, ".raw");
		break;
	case SAVE_MODE_DNG:
	case SAVE_MODE_TIFF:
	case SAVE_MODE_TIFF_RAW:
		break;
	}

	//If a file of this name already exists
	struct stat buffer;
	if(stat (path, &buffer) == 0)
	{
		return RECORD_FILE_EXISTS;
	}

	//Check that the directory is writable
	if(access(fileDirectory, W_OK) != 0)
	{	//Not writable
		return RECORD_DIRECTORY_NOT_WRITABLE;
	}
	return SUCCESS;
}

CameraErrortype Video::startRecording(UInt32 sizeX, UInt32 sizeY, UInt32 start, UInt32 length, save_mode_type save_mode)
{
	QDBusPendingReply<QVariantMap> reply;
	QVariantMap map;
	UInt64 estFileSize;
	UInt32 realBitrate;
	char path[1000];

	/* Generate the desired filename, and check that we can write it. */
	int ret = mkfilename(path, save_mode);
	if(ret != SUCCESS) return (CameraErrortype)ret;

	/* Attempt to start the video recording process. */
	map.insert("filename", QVariant(path));
	map.insert("start", QVariant(start));
	map.insert("length", QVariant(length));
	switch(save_mode) {
	case SAVE_MODE_H264:
		realBitrate = min(bitsPerPixel * sizeX * sizeY * framerate, min(60000000, (UInt32)(maxBitrate * 1000000.0)));
		estFileSize = realBitrate * (length / framerate) / 8; /* size = (bits/sec) * (seconds) / (8 bits/byte) */
		map.insert("format", QVariant("h264"));
		map.insert("bitrate", QVariant((uint)realBitrate));
		map.insert("framerate", QVariant((uint)framerate));
		break;
	case SAVE_MODE_RAW16:
		estFileSize = 16 * sizeX * sizeY * length / 8;
		map.insert("format", QVariant("y16"));
		break;
	case SAVE_MODE_RAW12:
		estFileSize = 12 * sizeX * sizeY * length / 8;
		map.insert("format", QVariant("y12b"));
		break;
	case SAVE_MODE_DNG:
		estFileSize = 16 * sizeX * sizeY * length / 8;
		estFileSize += (4096 * length);
		map.insert("format", QVariant("dng"));
		break;
	case SAVE_MODE_TIFF:
		estFileSize = 24 * sizeX * sizeY * length / 8;
		estFileSize += (4096 * length);
		map.insert("format", QVariant("tiff"));
		break;
	case SAVE_MODE_TIFF_RAW:
		estFileSize = 16 * sizeX * sizeY * length / 8;
		estFileSize += (4096 * length);
		map.insert("format", QVariant("tiffraw"));
		break;
	}
	printf("Saving video to %s\r\n", path);

	/* Send the DBus command to be*/
	pthread_mutex_lock(&mutex);
	reply = iface.recordfile(map);
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		qDebug() << "Recording failed with error " << err.message();
		return RECORD_ERROR;
	}
	else {
		return SUCCESS;
	}
}

CameraErrortype Video::stopRecording()
{
	QDBusPendingReply<QVariantMap> reply;
	QVariantMap map;

	pthread_mutex_lock(&mutex);
	reply = iface.stop();
	reply.waitForFinished();
	pthread_mutex_unlock(&mutex);
	if (reply.isError()) {
		return RECORD_ERROR;
	}
	map = reply.value();
	return SUCCESS;
}

void Video::setDisplayPosition(bool videoOnRight)
{
	displayWindowXOff = videoOnRight ? 200 : 0;
	displayWindowYOff = 0;

	QVariantMap args;
	QDBusPendingReply<QVariantMap> reply;
	args.insert("hres", QVariant(displayWindowXSize));
	args.insert("vres", QVariant(displayWindowYSize));
	args.insert("xoff", QVariant(displayWindowXOff));
	args.insert("yoff", QVariant(displayWindowYOff));

	pthread_mutex_lock(&mutex);
	reply = iface.configure(args);
	pthread_mutex_unlock(&mutex);

	if (reply.isError()) {
		QDBusError err = reply.error();
		fprintf(stderr, "Failed to configure horizontal offset: %s - %s\n", err.name().data(), err.message().toAscii().data());
	}
}

void Video::sof(const QVariantMap &args)
{
	emit started(parseVideoState(args));
}

void Video::eof(const QVariantMap &args)
{
	if (args.contains("error")) {
		emit ended(parseVideoState(args), args["error"].toString());
	} else {
		emit ended(parseVideoState(args), QString());
	}
}

void Video::segment(const QVariantMap &args)
{
	static VideoStatus st;
	emit newSegment(parseVideoStatus(args, &st));
}

Video::Video() : iface("ca.krontech.chronos.video", "/ca/krontech/chronos/video", QDBusConnection::systemBus())
{
	QDBusConnection conn = iface.connection();
	int i;

	pid = -1;
	running = false;

	/* Default video geometry */
	displayWindowXSize = 600;
	displayWindowYSize = 480;
	displayWindowXOff = 0;
	displayWindowYOff = 0;

	/* Prepare the recording parameters */
	bitsPerPixel = 0.7;
	maxBitrate = 40.0;
	framerate = 60;
	profile = OMX_H264ENC_PROFILE_HIGH;
	level = OMX_H264ENC_LVL_51;
	strcpy(filename, "");

	/* Set the default file path, or fall back to the MMC card. */
	for (i = 1; i <= 3; i++) {
		sprintf(fileDirectory, "/media/sda%d", i);
		if (path_is_mounted(fileDirectory)) {
			break;
		}
	}
	strcpy(fileDirectory, "/media/mmcblk1p1");

	pthread_mutex_init(&mutex, NULL);

	/* Connect DBus signals */
	conn.connect("ca.krontech.chronos.video", "/ca/krontech/chronos/video", "ca.krontech.chronos.video",
				 "sof", this, SLOT(sof(const QVariantMap&)));
	conn.connect("ca.krontech.chronos.video", "/ca/krontech/chronos/video", "ca.krontech.chronos.video",
				 "eof", this, SLOT(eof(const QVariantMap&)));
	conn.connect("ca.krontech.chronos.video", "/ca/krontech/chronos/video", "ca.krontech.chronos.video",
				 "segment", this, SLOT(segment(const QVariantMap&)));

	/* Try to get the PID of the video pipeline. */
	checkpid();
}

Video::~Video()
{
	pthread_mutex_destroy(&mutex);
}

