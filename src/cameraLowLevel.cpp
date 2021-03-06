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
#include <stdio.h>
#include <fcntl.h>
#include <QDebug>
#include <unistd.h>

#include "camera.h"
#include "gpmc.h"
#include "gpmcRegs.h"
#include "util.h"
#include "cameraRegisters.h"
extern "C" {
#include "eeprom.h"
}
#include "defines.h"

bool Camera::getRecDataFifoIsEmpty(void)
{
	return gpmc->read32(SEQ_STATUS_ADDR) & SEQ_STATUS_MD_FIFO_EMPTY_MASK;
}

UInt32 Camera::readRecDataFifo(void)
{
	return gpmc->read32(SEQ_MD_FIFO_READ_ADDR);
}

bool Camera::getRecording(void)
{
	return gpmc->read32(SEQ_STATUS_ADDR) & SEQ_STATUS_RECORDING_MASK;
}

void Camera::startSequencer(void)
{
	UInt32 reg = gpmc->read32(SEQ_CTL_ADDR);
	gpmc->write32(SEQ_CTL_ADDR, reg | SEQ_CTL_START_REC_MASK);
	gpmc->write32(SEQ_CTL_ADDR, reg & ~SEQ_CTL_START_REC_MASK);
}

void Camera::terminateRecord(void)
{
	UInt32 reg = gpmc->read32(SEQ_CTL_ADDR);
	gpmc->write32(SEQ_CTL_ADDR, reg | SEQ_CTL_STOP_REC_MASK);
	gpmc->write32(SEQ_CTL_ADDR, reg & ~SEQ_CTL_STOP_REC_MASK);
}

void Camera::writeSeqPgmMem(SeqPgmMemWord pgmWord, UInt32 address)
{
	gpmc->write32((SEQ_PGM_MEM_START_ADDR + address * 16)+4, pgmWord.data.high);
	gpmc->write32((SEQ_PGM_MEM_START_ADDR + address * 16)+0, pgmWord.data.low/*0x00282084*/);
}

void Camera::setRecRegion(UInt32 start, UInt32 count, FrameGeometry *geometry)
{
	UInt32 sizeWords = getFrameSizeWords(geometry);
	gpmc->write32(SEQ_FRAME_SIZE_ADDR, sizeWords);
	gpmc->write32(SEQ_REC_REGION_START_ADDR, start);
	gpmc->write32(SEQ_REC_REGION_END_ADDR, start + count * sizeWords);
}

/* Camera::readIsColor
 *
 * Returns weather the camera is color or mono, read from the hardware jumper
 *
 * returns: true for color, false for mono
 **/

bool Camera::readIsColor(void)
{
	Int32 colorSelFD;

	colorSelFD = open("/sys/class/gpio/gpio34/value", O_RDONLY);

	if (-1 == colorSelFD)
		return CAMERA_FILE_ERROR;

	char buf[2];

	lseek(colorSelFD, 0, SEEK_SET);
	read(colorSelFD, buf, sizeof(buf));

	return ('1' == buf[0]) ? true : false;

	close(colorSelFD);
}

void Camera::setFocusPeakThresholdLL(UInt32 thresh)
{
	gpmc->write32(DISPLAY_PEAKING_THRESH_ADDR, thresh);
}

UInt32 Camera::getFocusPeakThresholdLL(void)
{
	return gpmc->read32(DISPLAY_PEAKING_THRESH_ADDR);
}

Int32 Camera::getRamSizeGB(UInt32 * stick0SizeGB, UInt32 * stick1SizeGB)
{
	int retVal;
	int file;
    unsigned char ram0_buf, ram1_buf;

	/* if we are reading, *WRITE* to file */
	if ((file = open(RAM_SPD_I2C_BUS_FILE, O_WRONLY|O_CREAT,0666)) < 0) {
		/* ERROR HANDLING: you can check errno to see what went wrong */
		qDebug() << "Failed to open i2c bus" << RAM_SPD_I2C_BUS_FILE;
		return CAMERA_FILE_ERROR;
	}

//	Print out all the SPD data
//	for(int i = 0; i < 256; i++)
//	{
//		retVal = eeprom_read(file, RAM_SPD_I2C_ADDRESS_STICK_0/*Address*/, i/*Offset*/, &buf/*buffer*/, sizeof(buf)/*Length*/);
//		qDebug() << buf;
//	}

    // Read ram slot 0 size
    retVal = eeprom_read(file, RAM_SPD_I2C_ADDRESS_STICK_0/*Address*/, 5/*Offset*/, &ram0_buf/*buffer*/, sizeof(ram0_buf)/*Length*/);

    if(retVal < 0)
    {
        *stick0SizeGB = 0;
    }


    switch(ram0_buf & 0x07)	//Column size is in bits 2:0, and is row address width - 9
    {
        case 1:	//8GB, column address width is 10
            *stick0SizeGB = 8;

            break;
        case 2:	//16GB, column address width is 11
            *stick0SizeGB = 16;
            break;
        default:
        *stick0SizeGB = 0;
            break;
    }

    qDebug() << "Found" << *stick0SizeGB << "GB memory stick in slot 0";


    // Read ram slot 1 size
    retVal = eeprom_read(file, RAM_SPD_I2C_ADDRESS_STICK_1/*Address*/, 5/*Offset*/, &ram1_buf/*buffer*/, sizeof(ram1_buf)/*Length*/);

    if(retVal < 0)
    {
        *stick1SizeGB = 0;
    }
    else {
        switch(ram1_buf & 0x07)	//Column size is in bits 2:0, and is row address width - 9
        {
            case 1:	//8GB, column address width is 10
                *stick1SizeGB = 8;

                break;
            case 2:	//16GB, column address width is 11
                *stick1SizeGB = 16;
                break;
            default:
                *stick1SizeGB = 0;
                break;
        }
    }

    qDebug() << "Found" << *stick1SizeGB << "GB memory stick in slot 1";

    close(file);

    if (*stick0SizeGB == 0 && *stick1SizeGB == 0) {
        return CAMERA_ERROR_IO;
    }

    return SUCCESS;
}

//dest must be a char array that can handle SERIAL_NUMBER_MAX_LEN + 1 bytes
Int32 Camera::readSerialNumber(char * dest)
{
	int retVal;
	int file;

	/* if we are reading, *WRITE* to file */
	if ((file = open(RAM_SPD_I2C_BUS_FILE, O_WRONLY|O_CREAT,0666)) < 0) {
		/* ERROR HANDLING: you can check errno to see what went wrong */
		qDebug() << "Failed to open i2c bus" << RAM_SPD_I2C_BUS_FILE;
		return CAMERA_FILE_ERROR;
	}

	retVal = eeprom_read_large(file, CAMERA_EEPROM_I2C_ADDR/*Address*/, SERIAL_NUMBER_OFFSET/*Offset*/, (unsigned char *)dest/*buffer*/, SERIAL_NUMBER_MAX_LEN/*Length*/);
	close(file);

	if(retVal < 0)
	{
		return CAMERA_ERROR_IO;
	}

	qDebug() << "Read in serial number" << dest;
	dest[SERIAL_NUMBER_MAX_LEN] = '\0';

	return SUCCESS;
}

Int32 Camera::writeSerialNumber(char * src)
{
	int retVal;
	int file;
	char serialNumber[SERIAL_NUMBER_MAX_LEN];

	memset(serialNumber, 0x00, SERIAL_NUMBER_MAX_LEN);

	if (strlen(src) > SERIAL_NUMBER_MAX_LEN) {
		// forcefully null terminate string
		src[SERIAL_NUMBER_MAX_LEN] = 0;
	}
	
	strcpy(serialNumber, src);
	
	const char *filename = RAM_SPD_I2C_BUS_FILE;

	/* if we are writing to eeprom, *READ* from file */
	if ((file = open(filename, O_RDONLY)) < 0) {
		/* ERROR HANDLING: you can check errno to see what went wrong */
		qDebug() << "Failed to open the i2c bus";
		return CAMERA_FILE_ERROR;
	}

	retVal = eeprom_write_large(file, CAMERA_EEPROM_I2C_ADDR, SERIAL_NUMBER_OFFSET, (unsigned char *) serialNumber, SERIAL_NUMBER_MAX_LEN);
	qDebug("eeprom_write_large returned: %d", retVal);
	::close(file);

	delayms(250);

	return SUCCESS;
}

UInt16 Camera::getFPGAVersion(void)
{
	return gpmc->read16(FPGA_VERSION_ADDR);
}

UInt16 Camera::getFPGASubVersion(void)
{
	return gpmc->read16(FPGA_SUBVERSION_ADDR);
}
