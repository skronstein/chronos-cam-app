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
#include "math.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <QResource>
#include <QDir>
#include <QIODevice>

#include "spi.h"
#include "util.h"

#include "defines.h"
#include "cameraRegisters.h"



#include "types.h"
#include "lux2100.h"

#include <QSettings>

/* Select binning or windowed mode. */
#define LUX2100_BINNING_MODE	1

//1080p binned
//Binned half length
UInt8 LUX2100_sram66Clk[414] = {0x80, 0x00, 0x00, 0x20, 0x08, 0xD0, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x00, 0x09, 0x53, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x53, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x1C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x1C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0xA0, 0x98, 0x00, 0x80, 0x01, 0x43, 0x80, 0x98, 0x00, 0x00, 0x01, 0x43, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

//Full resolution 4k
//Non-binned full length
UInt8 LUX8M_sram126Clk[774] =  {0x80, 0x00, 0x00, 0x20, 0x08, 0xD0, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x00, 0x09, 0x53, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x53,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x1C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x1C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x1C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x98, 0x00, 0x80, 0x01, 0x43, 0x80, 0x98,
                                0x00, 0x00, 0x01, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

//1080p windowed from 4k
//Non-binned half length
UInt8 LUX8M_sram66ClkFRWindow[774] =
                               {0x80, 0x00, 0x00, 0x20, 0x08, 0xD0, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20, 0x09, 0xD3, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20,
                                0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08,
                                0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x20, 0x09, 0x53,
                                0x80, 0x08, 0x00, 0x20, 0x09, 0x53, 0x80, 0x08, 0x00, 0x00, 0x09, 0x53, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x53, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x3C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x1C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x1C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80,
                                0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C,
                                0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43, 0x80, 0x9C, 0x00, 0x80, 0x01, 0x43,
                                0x80, 0x98, 0x00, 0x80, 0x01, 0x43, 0x80, 0x98, 0x00, 0x00, 0x01, 0x43, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


#define round(x) (floor(x + 0.5))
//#define SCI_DEBUG_PRINTS

LUX2100::LUX2100()
{
	spi = new SPI();
	wavetableSelect = LUX2100_WAVETABLE_AUTO;
	startDelaySensorClocks = LUX2100_MAGIC_ABN_DELAY;
	//Zero out offsets
	for(int i = 0; i < LUX2100_HRES_INCREMENT; i++)
		offsetsA[i] = 0;

}

LUX2100::~LUX2100()
{
	delete spi;
}

CameraErrortype LUX2100::init(GPMC * gpmc_inst)
{
	CameraErrortype retVal;
	retVal = spi->Init(IMAGE_SENSOR_SPI, 16, 1000000, false, true);	//Invert clock phase
	if(SUCCESS != retVal)
		return retVal;

	dacCSFD = open("/sys/class/gpio/gpio33/value", O_WRONLY);
	if (-1 == dacCSFD)
		return LUX1310_FILE_ERROR;

	gpmc = gpmc_inst;

	retVal = initSensor();
	//mem problem before this
	if(SUCCESS != retVal)
		return retVal;

	return SUCCESS;
}

CameraErrortype LUX2100::initSensor()
{
	wavetableSize = 66;
	gain = LUX2100_GAIN_1;

	gpmc->write32(IMAGER_FRAME_PERIOD_ADDR, 100*4000);	//Disable integration
	gpmc->write32(IMAGER_INT_TIME_ADDR, 100*4100);

	initDAC();

#if LUX2100_BINNING_MODE
	initLUX2100(false);
#else
	initLUX8M();
#endif


	delayms(10);

    gpmc->write32(IMAGER_FRAME_PERIOD_ADDR, 100000000/100);	//Enable integration
    gpmc->write32(IMAGER_INT_TIME_ADDR, 100000000/200);

	delayms(50);


	currentRes.hRes = LUX2100_MAX_H_RES;
	currentRes.vRes = LUX2100_MAX_V_RES;
	currentRes.hOffset = 0;
	currentRes.vOffset = 0;
	currentRes.vDarkRows = 0;
	currentRes.bitDepth = LUX2100_BITS_PER_PIXEL;
	setFramePeriod(getMinFramePeriod(&currentRes)/100000000.0, &currentRes);
	//mem problem before this
	setIntegrationTime((double)getMaxExposure(currentPeriod) / 100000000.0, &currentRes);

	return SUCCESS;
}

void LUX2100::SCIWrite(UInt8 address, UInt16 data)
{
	int i = 0;

	//qDebug() << "sci write" << data;

	//Clear RW and reset FIFO
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, 0x8000 | (gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & ~SENSOR_SCI_CONTROL_RW_MASK));

	//Set up address, transfer length and put data into FIFO
	gpmc->write16(SENSOR_SCI_ADDRESS_ADDR, address);
	gpmc->write16(SENSOR_SCI_DATALEN_ADDR, 2);
	gpmc->write16(SENSOR_SCI_FIFO_WR_ADDR_ADDR, data >> 8);
	gpmc->write16(SENSOR_SCI_FIFO_WR_ADDR_ADDR, data & 0xFF);

	//Start transfer
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, gpmc->read16(SENSOR_SCI_CONTROL_ADDR) | SENSOR_SCI_CONTROL_RUN_MASK);

	int first = gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK;
	//Wait for completion
	while(gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK)
		i++;

#ifdef SCI_DEBUG_PRINTS
	if(!first && i != 0)
		qDebug() << "First busy was missed, address:" << address;
	else if(i == 0)
		qDebug() << "No busy detected, something is probably very wrong, address:" << address;

	int readback = SCIRead(address);
	int readback2 = SCIRead(address);
	int readback3 = SCIRead(address);
	if(data != readback)
		qDebug() << "SCI readback wrong, address: " << address << " expected: " << data << " got: " << readback << readback2 << readback3;
#endif
}

void LUX2100::SCIWriteBuf(UInt8 address, UInt8 * data, UInt32 dataLen)
{
	//Clear RW and reset FIFO
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, 0x8000 | (gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & ~SENSOR_SCI_CONTROL_RW_MASK));

	//Set up address, transfer length and put data into FIFO
	gpmc->write16(SENSOR_SCI_ADDRESS_ADDR, address);
	gpmc->write16(SENSOR_SCI_DATALEN_ADDR, dataLen);
	for(UInt32 i = 0; i < dataLen; i++)
		gpmc->write16(SENSOR_SCI_FIFO_WR_ADDR_ADDR, data[i]);

	//Start transfer
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, gpmc->read16(SENSOR_SCI_CONTROL_ADDR) | SENSOR_SCI_CONTROL_RUN_MASK);

	//Wait for completion
	while(gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK);
}

UInt16 LUX2100::SCIRead(UInt8 address)
{
	int i = 0;

	//Set RW
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, gpmc->read16(SENSOR_SCI_CONTROL_ADDR) | SENSOR_SCI_CONTROL_RW_MASK);

	//Set up address and transfer length
	gpmc->write16(SENSOR_SCI_ADDRESS_ADDR, address);
	gpmc->write16(SENSOR_SCI_DATALEN_ADDR, 2);

	//Start transfer
	gpmc->write16(SENSOR_SCI_CONTROL_ADDR, gpmc->read16(SENSOR_SCI_CONTROL_ADDR) | SENSOR_SCI_CONTROL_RUN_MASK);

	int first = gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK;
	//Wait for completion
	while(gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK)
		i++;

#ifdef SCI_DEBUG_PRINTS
	if(!first && i != 0)
		qDebug() << "Read First busy was missed, address:" << address;
	else if(i == 0)
		qDebug() << "Read No busy detected, something is probably very wrong, address:" << address;
#endif

	//Wait for completion
//	while(gpmc->read16(SENSOR_SCI_CONTROL_ADDR) & SENSOR_SCI_CONTROL_RUN_MASK)
//		count++;
	delayms(1);
	return gpmc->read16(SENSOR_SCI_READ_DATA_ADDR);
}

Int32 LUX2100::setOffset(UInt16 * offsets)
{
	return 0;
	for(int i = 0; i < 16; i++)
		SCIWrite(0x3a+i, offsets[i]); //internal control register
}

CameraErrortype LUX2100::autoPhaseCal(void)
{
	UInt16 valid = 0;
	UInt32 valid32;

	setClkPhase(0);
	setClkPhase(1);

	setClkPhase(0);


	int dataCorrect = getDataCorrect();
	qDebug() << "datacorrect" << dataCorrect;
	return SUCCESS;

//	setSyncToken(0x32A);	//Set sync token to lock to the training pattern (0x32A or 1100101010)

	for(int i = 0; i < 16; i++)
	{
		valid = (valid >> 1) | ((0x1FFFF == getDataCorrect()) ? 0x8000 : 0);

		qDebug() << "datacorrect" << clkPhase << " " << getDataCorrect();
		//advance clock phase
		clkPhase = getClkPhase() + 1;
		if(clkPhase >= 16)
			clkPhase = 0;
		setClkPhase(clkPhase);
	}

	if(0 == valid)
	{
		setClkPhase(4);
		return LUPA1300_NO_DATA_VALID_WINDOW;
	}

	qDebug() << "Valid: " << valid;

	valid32 = valid | (valid << 16);

	//Determine the start and length of the window of clock phase values that produce valid outputs
	UInt32 bestMargin = 0;
	UInt32 bestStart = 0;

	for(int i = 0; i < 16; i++)
	{
		UInt32 margin = 0;
		if(valid32 & (1 << i))
		{
			//Scan starting at this valid point
			for(int j = 0; j < 16; j++)
			{
				//Track the margin until we hit a non-valid point
				if(valid32 & (1 << (i + j)))
					margin++;
				else
				{
					//Track the best
					if(margin > bestMargin)
					{
						bestMargin = margin;
						bestStart = i;
					}
				}
			}
		}
	}

	if(bestMargin <= 3)
		return LUPA1300_INSUFFICIENT_DATA_VALID_WINDOW;

	//Set clock phase to the best
	clkPhase = (bestStart + bestMargin / 2) % 16;
	setClkPhase(clkPhase);
	qDebug() << "Valid Window start: " << bestStart << "Valid Window Length: " << bestMargin << "clkPhase: " << clkPhase;
}

Int32 LUX2100::seqOnOff(bool on)
{
	if (on) {
		gpmc->write32(IMAGER_INT_TIME_ADDR, currentExposure);
	} else {
		gpmc->write32(IMAGER_INT_TIME_ADDR, 0);	//Disable integration
	}
	return SUCCESS;
}

void LUX2100::setReset(bool reset)
{
		gpmc->write16(IMAGE_SENSOR_CONTROL_ADDR, (gpmc->read16(IMAGE_SENSOR_CONTROL_ADDR) & ~IMAGE_SENSOR_RESET_MASK) | (reset ? IMAGE_SENSOR_RESET_MASK : 0));
}

void LUX2100::setClkPhase(UInt8 phase)
{
		gpmc->write16(IMAGE_SENSOR_CLK_PHASE_ADDR, phase);
}

UInt8 LUX2100::getClkPhase(void)
{
		return gpmc->read16(IMAGE_SENSOR_CLK_PHASE_ADDR);
}

UInt32 LUX2100::getDataCorrect(void)
{
	return gpmc->read32(IMAGE_SENSOR_DATA_CORRECT_ADDR);
}

void LUX2100::setSyncToken(UInt16 token)
{
	gpmc->write16(IMAGE_SENSOR_SYNC_TOKEN_ADDR, token);
}

FrameGeometry LUX2100::getMaxGeometry(void)
{
	FrameGeometry size = {
		.hRes = LUX2100_MAX_H_RES,
		.vRes = LUX2100_MAX_V_RES,
		.hOffset = 0,
		.vOffset = 0,
		.vDarkRows = LUX2100_MAX_V_DARK,
		.bitDepth = LUX2100_BITS_PER_PIXEL,
	};
	return size;
}

void LUX2100::setResolution(FrameGeometry *size)
{
	UInt32 hStartBlocks = size->hOffset / LUX2100_HRES_INCREMENT;
	UInt32 hEndblocks = hStartBlocks + (size->hRes / LUX2100_HRES_INCREMENT);
	UInt32 vLastRow = LUX2100_MAX_V_RES + LUX2100_LOW_BOUNDARY_ROWS + LUX2100_HIGH_BOUNDARY_ROWS + LUX2100_HIGH_DARK_ROWS;

#if LUX2100_BINNING_MODE
	/* Binned operation - everything is x2 because it's really a 4K sensor. */
	SCIWrite(0x06, (LUX2100_LEFT_DARK_COLUMNS + hStartBlocks * LUX2100_HRES_INCREMENT) * 2);
	SCIWrite(0x07, (LUX2100_LEFT_DARK_COLUMNS + hEndblocks * LUX2100_HRES_INCREMENT) * 2 - 1);
	SCIWrite(0x08, (LUX2100_LOW_BOUNDARY_ROWS + size->vOffset * 2));
	SCIWrite(0x09, (LUX2100_LOW_BOUNDARY_ROWS + size->vOffset + size->vRes - 1) * 2);
	if (size->vDarkRows) {
		SCIWrite(0x2A, (vLastRow - size->vDarkRows) * 2);
	}
	SCIWrite(0x2B, size->vDarkRows * 2);
#else
	/* Windowed operation - add extra offset to put the readout at the centre. */
	SCIWrite(0x06, (LUX2100_LEFT_DARK_COLUMNS * 2 + 0x3E0) + hStartBlocks * LUX2100_HRES_INCREMENT);
	SCIWrite(0x07, (LUX2100_LEFT_DARK_COLUMNS * 2 + 0x3E0) + hEndblocks * LUX2100_HRES_INCREMENT - 1);
	SCIWrite(0x08, (LUX2100_LOW_BOUNDARY_ROWS * 2 + 0x22C) + size->vOffset);
	SCIWrite(0x09, (LUX2100_LOW_BOUNDARY_ROWS * 2 + 0x22C) + size->vOffset + size->vRes - 1);
	if (size->vDarkRows) {
		SCIWrite(0x2A, (vLastRow * 2) - size->vDarkRows);
	}
	SCIWrite(0x2B, size->vDarkRows);
#endif

	memcpy(&currentRes, size, sizeof(FrameGeometry));
}

bool LUX2100::isValidResolution(FrameGeometry *size)
{
	/* Enforce resolution limits. */
	if ((size->hRes < LUX2100_MIN_HRES) || (size->hRes + size->hOffset > LUX2100_MAX_H_RES)) {
		return false;
	}
	if ((size->vRes < LUX2100_MIN_VRES) || (size->vRes + size->vOffset > LUX2100_MAX_V_RES)) {
		return false;
	}
	if (size->vDarkRows > LUX2100_MAX_V_DARK) {
		return false;
	}
	if (size->bitDepth != LUX2100_BITS_PER_PIXEL) {
		return false;
	}
	/* Enforce minimum pixel increments. */
	if ((size->hRes % LUX2100_HRES_INCREMENT) || (size->hOffset % LUX2100_HRES_INCREMENT)) {
		return false;
	}
	if ((size->vRes % LUX2100_VRES_INCREMENT) || (size->vOffset % LUX2100_VRES_INCREMENT)) {
		return false;
	}
	if (size->vDarkRows % LUX2100_VRES_INCREMENT) {
		return false;
	}
	/* Otherwise, the resultion and offset are valid. */
	return true;
}

//Used by init functions only
UInt32 LUX2100::getMinFramePeriod(FrameGeometry *frameSize, UInt32 wtSize)
{
	if(!isValidResolution(frameSize))
		return 0;

//	if(hRes == 1280)
//		wtSize = 80;

	double tRead = (double)(frameSize->hRes / LUX2100_HRES_INCREMENT) * LUX2100_CLOCK_PERIOD;
	double tHBlank = 2.0 * LUX2100_CLOCK_PERIOD;
	double tWavetable = wtSize * LUX2100_CLOCK_PERIOD;
	double tRow = max(tRead+tHBlank, tWavetable+3*LUX2100_CLOCK_PERIOD);
	double tTx = 50 * LUX2100_CLOCK_PERIOD;
	double tFovf = 50 * LUX2100_CLOCK_PERIOD;
	double tFovb = (50) * LUX2100_CLOCK_PERIOD;//Duration between PRSTN falling and TXN falling (I think)
	double tFrame = tRow * (frameSize->vRes + frameSize->vDarkRows) + tTx + tFovf + tFovb;
	qDebug() << "getMinFramePeriod:" << tFrame;
	return (UInt64)(ceil(tFrame * 100000000.0));
}

double LUX2100::getMinMasterFramePeriod(FrameGeometry *frameSize)
{
	if(!isValidResolution(frameSize))
		return 0.0;

	return (double)getMinFramePeriod(frameSize) / 100000000.0;
}

UInt32 LUX2100::getMaxExposure(UInt32 period)
{
	return period - 500;
}

//Returns the period the sensor is set to in seconds
double LUX2100::getCurrentFramePeriodDouble(void)
{
	return (double)currentPeriod / 100000000.0;
}

//Returns the exposure time the sensor is set to in seconds
double LUX2100::getCurrentExposureDouble(void)
{
	return (double)currentExposure / 100000000.0;
}

double LUX2100::getActualFramePeriod(double targetPeriod, FrameGeometry *size)
{
	//Round to nearest 10ns period
	targetPeriod = round(targetPeriod * (100000000.0)) / 100000000.0;

	double minPeriod = getMinMasterFramePeriod(size);
	double maxPeriod = LUX2100_MAX_SLAVE_PERIOD;

	return within(targetPeriod, minPeriod, maxPeriod);
}

double LUX2100::setFramePeriod(double period, FrameGeometry *size)
{
	//Round to nearest 10ns period
	period = round(period * (100000000.0)) / 100000000.0;
	qDebug() << "Requested period" << period;
	double minPeriod = getMinMasterFramePeriod(size);
	double maxPeriod = LUX2100_MAX_SLAVE_PERIOD / 100000000.0;

	period = within(period, minPeriod, maxPeriod);
	currentPeriod = period * 100000000.0;

	setSlavePeriod(currentPeriod);
	return period;
}

/* getMaxIntegrationTime
 *
 * Gets the actual maximum integration time the sensor can be set to for the given settings
 *
 * intTime:	Desired integration time in seconds
 *
 * returns: Maximum integration time
 */
double LUX2100::getMaxIntegrationTime(double period, FrameGeometry *size)
{
	//Round to nearest 10ns period
	period = round(period * (100000000.0)) / 100000000.0;
	return (double)getMaxExposure(period * 100000000.0) / 100000000.0;

}

/* getMaxCurrentIntegrationTime
 *
 * Gets the actual maximum integration for the current settings
 *
 * returns: Maximum integration time
 */
double LUX2100::getMaxCurrentIntegrationTime(void)
{
	return (double)getMaxExposure(currentPeriod) / 100000000.0;
}

/* getActualIntegrationTime
 *
 * Gets the actual integration time that the sensor can be set to which is as close as possible to desired
 *
 * intTime:	Desired integration time in seconds
 *
 * returns: Actual closest integration time
 */
double LUX2100::getActualIntegrationTime(double intTime, double period, FrameGeometry *size)
{

	//Round to nearest 10ns period
	period = round(period * (100000000.0)) / 100000000.0;
	intTime = round(intTime * (100000000.0)) / 100000000.0;

	double maxIntTime = (double)getMaxExposure(period * 100000000.0) / 100000000.0;
    double minIntTime = LUX2100_MIN_INT_TIME;
	return within(intTime, minIntTime, maxIntTime);

}

/* setIntegrationTime
 *
 * Sets the integration time of the image sensor to a value as close as possible to requested
 *
 * intTime:	Desired integration time in seconds
 *
 * returns: Actual integration time that was set
 */
double LUX2100::setIntegrationTime(double intTime, FrameGeometry *size)
{
	//Round to nearest 10ns period
	intTime = round(intTime * (100000000.0)) / 100000000.0;

	//Set integration time to within limits
	double maxIntTime = (double)getMaxExposure(currentPeriod) / 100000000.0;
    double minIntTime = LUX2100_MIN_INT_TIME;
	intTime = within(intTime, minIntTime, maxIntTime);
	currentExposure = intTime * 100000000.0;	
	setSlaveExposure(currentExposure);
	return intTime;
}

/* getIntegrationTime
 *
 * Gets the integration time of the image sensor
 *
 * returns: Integration tim
 */
double LUX2100::getIntegrationTime(void)
{

	return (double)currentExposure / 100000000.0;
}


void LUX2100::setSlavePeriod(UInt32 period)
{
	gpmc->write32(IMAGER_FRAME_PERIOD_ADDR, period);
}

void LUX2100::setSlaveExposure(UInt32 exposure)
{
	//hack to fix line issue. Not perfect, need to properly register this on the sensor clock.
	double linePeriod = max((currentRes.hRes / LUX2100_HRES_INCREMENT)+2, (wavetableSize + 3)) * 1.0/LUX2100_SENSOR_CLOCK;	//Line period in seconds
	UInt32 startDelay = (double)startDelaySensorClocks * LUX2100_TIMING_CLOCK_FREQ / LUX2100_SENSOR_CLOCK;
	double targetExp = (double)exposure / 100000000.0;
	UInt32 expLines = round(targetExp / linePeriod);

	exposure = startDelay + (linePeriod * expLines * 100000000.0);
	//qDebug() << "linePeriod" << linePeriod << "startDelaySensorClocks" << startDelaySensorClocks << "startDelay" << startDelay
	//		 << "targetExp" << targetExp << "expLines" << expLines << "exposure" << exposure;
	gpmc->write32(IMAGER_INT_TIME_ADDR, exposure);
	currentExposure = exposure;
}

void LUX2100::dumpRegisters(void)
{
	return;
}

//Set up DAC
void LUX2100::initDAC()
{
	//Put DAC in Write through mode (DAC channels update immediately on register write)
	writeDACSPI(0x9000, 0x9000);
}

//Write the data into the selected channel
void LUX2100::writeDAC(UInt16 data, UInt8 channel)
{
    UInt16 dacVal = ((channel & 0x7) << 12) | (data & 0x0FFF);

    if((channel & 0x8) == 0)    //Writing to DAC 0
        writeDACSPI(dacVal, LUX2100_DAC_NOP_COMMAND);
    else
        writeDACSPI(LUX2100_DAC_NOP_COMMAND, dacVal);
}

void LUX2100::writeDACVoltage(UInt8 channel, float voltage)
{
	switch(channel)
	{
        case LUX2100_VABL_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VABL_SCALE), LUX2100_VABL_VOLTAGE);
			break;
        case LUX2100_VTX2L_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VTX2L_SCALE), LUX2100_VTX2L_VOLTAGE);
			break;
        case LUX2100_VTXH_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VTXH_SCALE), LUX2100_VTXH_VOLTAGE);
			break;
        case LUX2100_VDR1_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VDR1_SCALE), LUX2100_VDR1_VOLTAGE);
			break;
        case LUX2100_VRSTH_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VRSTH_SCALE), LUX2100_VRSTH_VOLTAGE);
			break;
        case LUX2100_VDR3_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VDR3_SCALE), LUX2100_VDR3_VOLTAGE);
			break;
        case LUX2100_VRDH_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VRDH_SCALE), LUX2100_VRDH_VOLTAGE);
			break;
        case LUX2100_VTX2H_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VTX2H_SCALE), LUX2100_VTX2H_VOLTAGE);
            break;
        case LUX2100_VTXL_VOLTAGE:
            writeDAC((UInt16)((LUX2100_VTXL_OFFSET - (float)voltage) * LUX2100_VTXL_SCALE), LUX2100_VTXL_VOLTAGE);
            break;
        case LUX2100_VPIX_OP_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VPIX_OP_SCALE), LUX2100_VPIX_OP_VOLTAGE);
            break;
        case LUX2100_VDR2_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VDR2_SCALE), LUX2100_VDR2_VOLTAGE);
            break;
        case LUX2100_VPIX_LDO_VOLTAGE:
            writeDAC((UInt16)((LUX2100_VPIX_LDO_OFFSET - (float)voltage) * LUX2100_VPIX_LDO_SCALE), LUX2100_VPIX_LDO_VOLTAGE);
            break;
        case LUX2100_VRSTPIX_VOLTAGE:
            writeDAC((UInt16)(voltage * LUX2100_VRSTPIX_SCALE), LUX2100_VRSTPIX_VOLTAGE);
        break;
		default:
			return;
        break;
	}
}

//Performs an SPI write to the DAC
int LUX2100::writeDACSPI(UInt16 data0, UInt16 data1)
{
	UInt8 tx[4];
	UInt8 rx[sizeof(tx)];
	int retval;

	tx[3] = data0 >> 8;
	tx[2] = data0 & 0xFF;
	tx[1] = data1 >> 8;
	tx[0] = data1 & 0xFF;

	setDACCS(false);
	//delayms(1);
	retval = spi->Transfer((uint64_t) tx, (uint64_t) rx, sizeof(tx));
	//delayms(1);
	setDACCS(true);
	return retval;
}

void LUX2100::setDACCS(bool on)
{
	lseek(dacCSFD, 0, SEEK_SET);
	write(dacCSFD, on ? "1" : "0", 1);
}

void LUX2100::updateWavetableSetting(bool gainCalMode)
{
	SCIWrite(0x04, 0x0000);	/* Switch to main register space */

	if (gainCalMode) {
		/* Switch to analog test mode. */
#if LUX2100_BINNING_MODE
		SCIWrite(0x56, 0xB121);
#else
		SCIWrite(0x56, 0xB001);
#endif

		/* Configure the desired test voltage. */
		SCIWrite(0x67, 0x6D11);
	}
	else {
		/* Disable the test voltage. */
		SCIWrite(0x67, 0x1E11);

		/* Disable analog test mode. */
#if LUX2100_BINNING_MODE
		SCIWrite(0x56, 0xA121);
#else
		SCIWrite(0x56, 0x9001);
#endif
	}
}

//Remaps data channels to ADC channel index
const char adcChannelRemap[] = {0, 8, 16, 24, 4, 12,20, 28,  2,10, 18, 26,  6, 14, 22, 30, 1, 9,  17, 25, 5, 13, 21, 29, 3, 11, 19, 27, 7, 15, 23, 31};

//Sets ADC offset for one channel
//Converts the input 2s complement value to the sensors's weird sign bit plus value format (sort of like float, with +0 and -0)
void LUX2100::setADCOffset(UInt8 channel, Int16 offset)
{
    UInt8 intChannel = adcChannelRemap[channel];
	offsetsA[channel] = offset;


    SCIWrite(0x04, 0x0001); // switch to datapath register space

	if(offset < 0)
    {
        SCIWrite(0x10 + (intChannel >= 16 ? 0x40 + intChannel - 16 : intChannel), (-offset & 0x3FF));
        SCIWrite(0x0F + (intChannel >= 16 ? 0x40 : 0x0), SCIRead(0x0F + (intChannel >= 16 ? 0x40 : 0x0)) | (1 << (intChannel & 0xF)));
    }
	else
    {
        SCIWrite(0x10 + (intChannel >= 16 ? 0x40 + intChannel - 16 : intChannel), offset & 0x3FF);
        SCIWrite(0x0F + (intChannel >= 16 ? 0x40 : 0x0), SCIRead(0x0F + (intChannel >= 16 ? 0x40 : 0x0)) & ~(1 << (intChannel & 0xF)));
    }

    SCIWrite(0x04, 0x0000); // switch back to sensor register space

}

//Gets ADC offset for one channel
//Don't use this, the values seem shifted right depending on the gain setting. Higher gain results in more shift. Seems to work fine at lower gains
Int16 LUX2100::getADCOffset(UInt8 channel)
{
    return 0;

	Int16 val = SCIRead(0x3a+channel);

	if(val & 0x400)
		return -(val & 0x3FF);
	else
		return val & 0x3FF;
}


//This doesn't seem to work. Sensor locks up
Int32 LUX2100::doAutoADCOffsetCalibration(void)
{
    /*
    SCIWrite(0x01, 0x0010); // disable the internal timing engine

    //SCIWrite(0x2A, 0x89A); // Address of first dark row to read out (half way through dark rows)
    //SCIWrite(0x2B, 1); // Readout 5 dark rows
    delayms(10);
    SCIWrite(0x01, 0x0011); // enable the internal timing engine
    delayms(10);
    */
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x0E, 0x0001); // Enable application of ADC offsets during v blank
    SCIWrite(0x0D, 0x0020); // ADC offset target
    SCIWrite(0x0A, 0x0001); // Start ADC Offset calibration
    delayms(2000);
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    return SUCCESS;

}

Int32 LUX2100::loadADCOffsetsFromFile(void)
{
	Int16 offsets[LUX2100_HRES_INCREMENT];

	QString filename;
	
	//Generate the filename for this particular resolution and offset
	filename.sprintf("cal:lux2100Offsets");
	
	std::string fn;
	fn = getFilename("", ".bin");
	filename.append(fn.c_str());
	QFileInfo adcOffsetsFile(filename);
	if (adcOffsetsFile.exists() && adcOffsetsFile.isFile()) 
		fn = adcOffsetsFile.absoluteFilePath().toLocal8Bit().constData();
	else 
		return CAMERA_FILE_NOT_FOUND;

	qDebug() << "attempting to load ADC offsets from" << fn.c_str();

	//If the offsets file exists, read it in
	if( access( fn.c_str(), R_OK ) == -1 )
		return CAMERA_FILE_NOT_FOUND;

	FILE * fp;
	fp = fopen(fn.c_str(), "rb");
	if(!fp)
		return CAMERA_FILE_ERROR;

	fread(offsets, sizeof(offsets[0]), LUX2100_HRES_INCREMENT, fp);
	fclose(fp);

	//Write the values into the sensor
	for(int i = 0; i < LUX2100_HRES_INCREMENT; i++)
		setADCOffset(i, offsets[i]);

	return SUCCESS;
}

Int32 LUX2100::saveADCOffsetsToFile(void)
{
	Int16 offsets[LUX2100_HRES_INCREMENT];
	std::string fn;

	fn = getFilename("cal/lux2100Offsets", ".bin");
	qDebug("writing ADC offsets to %s", fn.c_str());
	
	FILE * fp;
	fp = fopen(fn.c_str(), "wb");
	if(!fp)
		return CAMERA_FILE_ERROR;

	for(int i = 0; i < LUX2100_HRES_INCREMENT; i++)
		offsets[i] = offsetsA[i];

	//Ugly print to keep it all on one line
	qDebug() << "Saving offsets to file" << fn.c_str() << "Offsets:"
			 << offsets[0] << offsets[1] << offsets[2] << offsets[3]
			 << offsets[4] << offsets[5] << offsets[6] << offsets[7]
			 << offsets[8] << offsets[9] << offsets[10] << offsets[11]
			 << offsets[12] << offsets[13] << offsets[14] << offsets[15];

	//Store to file
	fwrite(offsets, sizeof(offsets[0]), LUX2100_HRES_INCREMENT, fp);
	fclose(fp);

	return SUCCESS;
}

//Generate a filename string used for calibration values that is specific to the current gain and wavetable settings
std::string LUX2100::getFilename(const char * filename, const char * extension)
{
	const char * gName, * wtName;

	switch(gain)
	{
		case LUX2100_GAIN_1:		gName = LUX2100_GAIN_1_FN;		break;
		case LUX2100_GAIN_2:		gName = LUX2100_GAIN_2_FN;		break;
		case LUX2100_GAIN_4:		gName = LUX2100_GAIN_4_FN;		break;
		case LUX2100_GAIN_8:		gName = LUX2100_GAIN_8_FN;		break;
		case LUX2100_GAIN_16:		gName = LUX2100_GAIN_16_FN;		break;
	default:						gName = "";						break;

	}

	switch(wavetableSize)
	{
		case 80:	wtName = LUX2100_WAVETABLE_80_FN; break;
		case 39:	wtName = LUX2100_WAVETABLE_39_FN; break;
		case 30:	wtName = LUX2100_WAVETABLE_30_FN; break;
		case 25:	wtName = LUX2100_WAVETABLE_25_FN; break;
		case 20:	wtName = LUX2100_WAVETABLE_20_FN; break;
		default:	wtName = "";					  break;
	}

	return std::string(filename) + "_" + gName + "_" + wtName + extension;
}


Int32 LUX2100::setGain(UInt32 gainSetting)
{
    return SUCCESS;

	switch(gainSetting)
	{
	case LUX2100_GAIN_1:	//1
		writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 3.6);

		//Set Gain
		SCIWrite(0x51, 0x007F);	//gain selection sampling cap (11)	12 bit
		SCIWrite(0x52, 0x007F);	//gain selection feedback cap (8) 7 bit
		SCIWrite(0x53, 0x03);	//Serial gain
	break;

	case LUX2100_GAIN_2:	//2
		writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 3.6);

		//Set Gain
		SCIWrite(0x51, 0x0FFF);	//gain selection sampling cap (11)	12 bit
		SCIWrite(0x52, 0x007F);	//gain selection feedback cap (8) 7 bit
		SCIWrite(0x53, 0x03);	//Serial gain
	break;

	case LUX2100_GAIN_4:	//4
		writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 3.6);

		//Set Gain
		SCIWrite(0x51, 0x0FFF);	//gain selection sampling cap (11)	12 bit
		SCIWrite(0x52, 0x007F);	//gain selection feedback cap (8) 7 bit
		SCIWrite(0x53, 0x00);	//Serial gain
	break;

	case LUX2100_GAIN_8:	//8
		writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 2.6);

		//Set Gain
		SCIWrite(0x51, 0x0FFF);	//gain selection sampling cap (11)	12 bit
		SCIWrite(0x52, 0x0007);	//gain selection feedback cap (8) 7 bit
		SCIWrite(0x53, 0x00);	//Serial gain
	break;

	case LUX2100_GAIN_16:	//16
		writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 2.6);

		//Set Gain
		SCIWrite(0x51, 0x0FFF);	//gain selection sampling cap (11)	12 bit
		SCIWrite(0x52, 0x0001);	//gain selection feedback cap (8) 7 bit
		SCIWrite(0x53, 0x00);	//Serial gain
	break;
	}

	gain = gainSetting;
	return SUCCESS;
}


// GR
// BG
//

UInt8 LUX2100::getFilterColor(UInt32 h, UInt32 v)
{
	if((v & 1) == 0)	//If on an even line
		return ((h & 1) == 0) ? FILTER_COLOR_GREEN : FILTER_COLOR_RED;
	else	//Odd line
		return ((h & 1) == 0) ? FILTER_COLOR_BLUE : FILTER_COLOR_GREEN;

}

Int32 LUX2100::setABNDelayClocks(UInt32 ABNOffset)
{
	gpmc->write16(SENSOR_MAGIC_START_DELAY_ADDR, ABNOffset);
	return SUCCESS;
}

Int32 LUX2100::LUX2100ADCBugCorrection(UInt16 * rawUnpackedFrame, UInt32 hRes, UInt32 vRes)
{
    UInt16 line[LUX2100_MAX_H_RES];

    for(unsigned int y = 0; y < vRes; y++)
    {
        memcpy(line, rawUnpackedFrame + y*hRes, sizeof(line)); //Copy the current line

        for(unsigned int x = 0; x < hRes; x++)
        {
            //Correct for first order effects 64 pixels out
            if(!((line[x] + 0x80) & 0x100) && line[x] >= 0x80 && x < (hRes - 64))
            {
                    rawUnpackedFrame[x+64+y*hRes] -= 0x8;
                    if(rawUnpackedFrame[x+64+y*hRes] > 4095)    //If it underflowed, clip to zero
                        rawUnpackedFrame[x+64+y*hRes] = 0;
            }

            //Correct for second order effects 96 pixels out
            if(!((line[x] + 0x20) & 0x40) && line[x] >= 0x20 && x < (hRes - 96))
            {
                    rawUnpackedFrame[x+96+y*hRes] -= 0x2;
                    if(rawUnpackedFrame[x+96+y*hRes] > 4095)    //If it underflowed, clip to zero
                        rawUnpackedFrame[x+96+y*hRes] = 0;
            }
        }
    }

    return SUCCESS;
}

Int32 LUX2100::initLUX2100(bool colorBinning)
{
    writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTX2L_VOLTAGE, 0.0);
    writeDACVoltage(LUX2100_VRSTPIX_VOLTAGE, 2.0);
    writeDACVoltage(LUX2100_VABL_VOLTAGE, 0.0);
    writeDACVoltage(LUX2100_VTXH_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTX2H_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTXL_VOLTAGE, 0.0);

    writeDACVoltage(LUX2100_VDR1_VOLTAGE, 2.5);
    writeDACVoltage(LUX2100_VDR2_VOLTAGE, 2.0);
    writeDACVoltage(LUX2100_VDR3_VOLTAGE, 1.5);
    writeDACVoltage(LUX2100_VRDH_VOLTAGE, 0.0);  //Datasheet says this should be grounded
    writeDACVoltage(LUX2100_VPIX_LDO_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VPIX_OP_VOLTAGE, 0.0);



    delayms(10);		//Settling time


    setReset(true);

    setReset(false);

    delayms(1);		//Seems to be required for SCI clock to get running

    SCIWrite(0x7E, 0);	//reset all registers
    //delayms(1);

    //Set gain to 2.6
    SCIWrite(0x57, 0x007F);     //gain = gain=(#1's in 0x57[11:0]+4)/(#1's in 0x58[6:0]+1) = 12/8 = 1.333
    SCIWrite(0x58, 0x037F);

//    SCIWrite(0x57, 0x01FF);     //gain = gain=(#1's in 0x57[11:0]+4)/(#1's in 0x58[6:0]+1) = 13/5 = 2.5
//    SCIWrite(0x58, 0x030F);
    SCIWrite(0x76, 0x0079);    //Serial gain setting V2: Serial Gain = (#1's in 0x76[8:4])/(#1's in 0x58[10:8]+1) = 3/3

    //Set up for 66Tclk wavetable
    SCIWrite(0x34, 0x0042); //Readout delay
    SCIWrite(0x30, 0x0A00); //Start of frame delay
    SCIWrite(0x56, colorBinning ? 0xA120 : 0xA121); //Set up binning mode for mono sensor
    SCIWrite(0x06, 0x0040); //Start of standard window (x direction)
    SCIWrite(0x07, 0x0F3F); //End of standard window (x direction)
    SCIWrite(0x08, 0x0010); //Start of standard window (Y direction)
    SCIWrite(0x09, 0x087E); //End of standard window (Y direction)

    //Internal control registers
    SCIWrite(0x5B, 0x0008); // line valid delay to match ADC latency
    SCIWrite(0x5F, 0x0000); // internal control register
    SCIWrite(0x78, 0x0803); // internal clock timing
    SCIWrite(0x2D, 0x0084); // state for idle controls
    SCIWrite(0x2E, 0x0000); // state for idle controls
    SCIWrite(0x2F, 0x0040); // state for idle controls
    SCIWrite(0x62, 0x2603); // ADC clock controls
    SCIWrite(0x60, 0x0300); // enable on chip termination
    SCIWrite(0x79, 0x0003); // internal control register
    SCIWrite(0x7D, 0x0001); // internal control register
    SCIWrite(0x6A, 0xAA88); // internal control register
    SCIWrite(0x6B, 0xAC88); // internal control register
    SCIWrite(0x6C, 0x8AAA); // internal control register
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x05, 0x0007); // delay to match ADC latency
    SCIWrite(0x04, 0x0000); // switch back to main register space


    //Training sequence
    SCIWrite(0x53, 0x0FC0); // pclk channel output during vertical blanking
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x1FC0); // always output custom digital pattern
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    //Sensor is now outputting 0xFC0 on all ports. Receiver will automatically sync when the clock phase is right
    autoPhaseCal();

    //Return to data valid mode
    SCIWrite(0x53, 0x0F00); // pclk channel output during vertical blanking
    SCIWrite(0x55, 0x0FC0); // pclk channel output during optical black
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x0AB8); // custom digital pattern for blanking output
    SCIWrite(0x05, 0x0007); // delay to match adc latency
    SCIWrite(0x04, 0x0000); // switch to sensor register space

    //Load the wavetable
    SCIWriteBuf(0x7F, LUX2100_sram66Clk, 414 /*sizeof(sram80Clk)*/);

    //Load the wavetable
    //SCIWriteBuf(0x7F, LUX8M_sram126Clk, 774 /*sizeof(sram80Clk)*/);

    for(int i = 0; i < LUX2100_HRES_INCREMENT; i++)
        setADCOffset(i, 0);

    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x0E, 0x0001); // Enable application of ADC offsets during v blank
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    SCIWrite(0x04, 0x0000); // switch to sensor register space


    //Enable timing engine
    SCIWrite(0x01, 0x0011); // enable the internal timing engine

    return SUCCESS;
}

Int32 LUX2100::initLUX8M_2()
{
    writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTX2L_VOLTAGE, 0.0);
    writeDACVoltage(LUX2100_VRSTPIX_VOLTAGE, 2.0);
    writeDACVoltage(LUX2100_VABL_VOLTAGE, 0.3);
    writeDACVoltage(LUX2100_VTXH_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTX2H_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTXL_VOLTAGE, 0.0);

    writeDACVoltage(LUX2100_VDR1_VOLTAGE, 2.5);
    writeDACVoltage(LUX2100_VDR2_VOLTAGE, 2.0);
    writeDACVoltage(LUX2100_VDR3_VOLTAGE, 1.5);
    writeDACVoltage(LUX2100_VRDH_VOLTAGE, 0.0);  //Datasheet says this should be grounded
    writeDACVoltage(LUX2100_VPIX_LDO_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VPIX_OP_VOLTAGE, 0.0);



    delayms(10);		//Settling time


    setReset(true);

    setReset(false);

    delayms(1);		//Seems to be required for SCI clock to get running

    SCIWrite(0x7E, 0);	//reset all registers
    //delayms(1);

    //Set gain to 2.6
    SCIWrite(0x57, 0x01FF);
    SCIWrite(0x58, 0x030F);
    SCIWrite(0x76, 0x0079);    //Serial gain setting

    //Set up for 66Tclk wavetable
     SCIWrite(0x34, 0x0080); //Readout delay
    SCIWrite(0x30, 0x0A00); //Start of frame delay
     SCIWrite(0x56, 0x1001); //Set ub binning mode for mono sensor
     SCIWrite(0x06, 0x0040); //Start of standard window (x direction)
     SCIWrite(0x07, 0x0F7F); //End of standard window (x direction)
     SCIWrite(0x08, 0x0000); //Start of standard window (Y direction)
     SCIWrite(0x09, 0x088F); //End of standard window (Y direction)


/*
    SCIWrite(0x34, 0x0080); //readout delay
    SCIWrite(0x30, 0x0A01); //start of frame delay
    SCIWrite(0x56, 0x1001); //set up operation mode
    SCIWrite(0x06, 0x0040); //Start of standard window (x direction)
    SCIWrite(0x07, 0x0F7F); //End of standard window (x direction)
    SCIWrite(0x08, 0x0000); //Start of standard window (Y direction)
    SCIWrite(0x09, 0x088F); //End of standard window (Y direction)
*/
    //Internal control registers
    SCIWrite(0x5B, 0x0008); // line valid delay to match ADC latency
    SCIWrite(0x5F, 0x0000); // internal control register
    SCIWrite(0x78, 0x0803); // internal clock timing
    SCIWrite(0x2D, 0x0084); // state for idle controls
    SCIWrite(0x2E, 0x0000); // state for idle controls
    SCIWrite(0x2F, 0x0040); // state for idle controls
    SCIWrite(0x62, 0x2603); // ADC clock controls
    SCIWrite(0x60, 0x0300); // enable on chip termination
    SCIWrite(0x79, 0x0003); // internal control register
    SCIWrite(0x7D, 0x0001); // internal control register
    SCIWrite(0x6A, 0xAA88); // internal control register
    SCIWrite(0x6B, 0xAC88); // internal control register
    SCIWrite(0x6C, 0x8AAA); // internal control register
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x05, 0x0007); // delay to match ADC latency
    SCIWrite(0x04, 0x0000); // switch back to main register space


    //Training sequence
    SCIWrite(0x53, 0x0FC0); // pclk channel output during vertical blanking
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x1FC0); // always output custom digital pattern
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    //Sensor is now outputting 0xFC0 on all ports. Receiver will automatically sync when the clock phase is right
    autoPhaseCal();

    //Return to data valid mode
    SCIWrite(0x53, 0x0F00); // pclk channel output during vertical blanking
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x0AB8); // custom digital pattern for blanking output
    SCIWrite(0x05, 0x0007); // delay to match adc latency
    SCIWrite(0x04, 0x0000); // switch to sensor register space

    //Load the wavetable
    SCIWriteBuf(0x7F, LUX8M_sram126Clk, 774 /*sizeof(sram80Clk)*/);

    for(int i = 0; i < LUX2100_HRES_INCREMENT; i++)
        setADCOffset(i, 0);

    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x0E, 0x0001); // Enable application of ADC offsets during v blank
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    SCIWrite(0x04, 0x0000); // switch to sensor register space


    //Enable timing engine
    SCIWrite(0x01, 0x0011); // enable the internal timing engine

    return SUCCESS;
}

Int32 LUX2100::initLUX8M()
{
    writeDACVoltage(LUX2100_VRSTH_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTX2L_VOLTAGE, 0.0);
    writeDACVoltage(LUX2100_VRSTPIX_VOLTAGE, 2.0);
    writeDACVoltage(LUX2100_VABL_VOLTAGE, 0.3);
    writeDACVoltage(LUX2100_VTXH_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTX2H_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VTXL_VOLTAGE, 0.0);

    writeDACVoltage(LUX2100_VDR1_VOLTAGE, 2.5);
    writeDACVoltage(LUX2100_VDR2_VOLTAGE, 2.0);
    writeDACVoltage(LUX2100_VDR3_VOLTAGE, 1.5);
    writeDACVoltage(LUX2100_VRDH_VOLTAGE, 0.0);  //Datasheet says this should be grounded
    writeDACVoltage(LUX2100_VPIX_LDO_VOLTAGE, 3.3);
    writeDACVoltage(LUX2100_VPIX_OP_VOLTAGE, 0.0);



    delayms(10);		//Settling time


    setReset(true);

    setReset(false);

    delayms(1);		//Seems to be required for SCI clock to get running

    SCIWrite(0x7E, 0);	//reset all registers
    //delayms(1);

   /* //Set gain to 2.6
    SCIWrite(0x57, 0x01FF);
    SCIWrite(0x58, 0x030F);
    SCIWrite(0x76, 0x0079);    //Serial gain setting
*/
    //Set up for 126Tclk wavetable

    //SCIWrite(0x34, 0x0080); //readout delay (full res)
    SCIWrite(0x34, 0x0042); //Readout delay (for half size wavetable)
    SCIWrite(0x30, 0x0A00); //start of frame delay. The datasheet says to write 0x0A01, but this causes the sensor to not output any data or sync codes other than H blank
    SCIWrite(0x56, 0x9001); //set up operation mode
    SCIWrite(0x06, 0x0040+0x3E0); //Start of standard window (x direction)
    SCIWrite(0x07, 0x07BF+0x3E0); //End of standard window (x direction)
    SCIWrite(0x08, 0x0010+0x22C); //Start of standard window (Y direction)
    SCIWrite(0x09, 0x0447+0x22C); //End of standard window (Y direction)
/*
    SCIWrite(0x06, 0x0040); //Start of standard window (x direction)
    SCIWrite(0x07, 0x07BF); //End of standard window (x direction)
    SCIWrite(0x08, 0x0010); //Start of standard window (Y direction)
    SCIWrite(0x09, 0x0447); //End of standard window (Y direction)
*/
    SCIWrite(0x79, 0x0133); //???

    SCIWrite(0x03, 0x0003); //H blank to 3 clocks

    SCIWrite(0x57, 0x01FF); //gain_sel_samp
    SCIWrite(0x58, 0x030F); //gain_serial, gain_sel_fb
    SCIWrite(0x76, 0x0079); //col_cap_en

    SCIWrite(0x5B, 0x0008); //lv_delay of 8
    SCIWrite(0x5F, 0x0000); //pwr_en_serializer_b (all seralizers enabled)
    SCIWrite(0x78, 0x0D03); //internal clock timing
    SCIWrite(0x62, 0x2603); //ADC clock controls
    SCIWrite(0x60, 0x0300); //disable on chip termination
    SCIWrite(0x66, 0x0000); //???
    SCIWrite(0x2D, 0x0084); //state for idle controls
    SCIWrite(0x2E, 0x0000); //state for idle controls
    SCIWrite(0x2F, 0x0040); //state for idle controls
    SCIWrite(0x7D, 0x0001); //internal control register
    SCIWrite(0x6A, 0xAA8A); //internal control register
    SCIWrite(0x6B, 0xA88A); //internal control register
    SCIWrite(0x6C, 0x89AA); //internal control register

    //Training sequence
    SCIWrite(0x53, 0x0FC0); // pclk channel output during vertical blanking
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x1FC0); // always output custom digital pattern
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    //Sensor is now outputting 0xFC0 on all ports. Receiver will automatically sync when the clock phase is right
    autoPhaseCal();
    //SCIWrite(0x0100, 0x0010);

    //Return to data valid mode
    SCIWrite(0x53, 0x0F00); // pclk channel output during vertical blanking
    SCIWrite(0x55, 0x0FC0); // pclk channel output during optical black
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x0AB8); // custom digital pattern for blanking output
    SCIWrite(0x05, 0x0007); // delay to match adc latency
    SCIWrite(0x0E, 0x0001); // enable applying adc offset registers during vertical blanking
    SCIWrite(0x04, 0x0000); // switch to sensor register space

    //Load the wavetable
    SCIWriteBuf(0x7F, LUX8M_sram66ClkFRWindow, 774 );



/*


    //Internal control registers

    SCIWrite(0x5B, 0x0008); //line valid delay to match adc latency
    SCIWrite(0x5F, 0x0000); //internal control register
    SCIWrite(0x78, 0x0803); //internal clock timing
    SCIWrite(0x2D, 0x0084); //state for idle controls
    SCIWrite(0x2E, 0x0000); //state for idle controls
    SCIWrite(0x2F, 0x0040); //state for idle controls
    SCIWrite(0x62, 0x2603); //ADC clock controls
    SCIWrite(0x60, 0x0300); //enable on chip termination
    SCIWrite(0x7D, 0x0001); //internal control register
    SCIWrite(0x6A, 0xAA35); //internal control register
    SCIWrite(0x6B, 0xA388); //internal control register
    SCIWrite(0x6C, 0x8AAA); //internal control register

    //Training sequence
    SCIWrite(0x53, 0x0FC0); // pclk channel output during vertical blanking
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x1FC0); // always output custom digital pattern
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    //Sensor is now outputting 0xFC0 on all ports. Receiver will automatically sync when the clock phase is right
    autoPhaseCal();

    //Return to data valid mode
    SCIWrite(0x53, 0x0F00); // pclk channel output during vertical blanking
    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x03, 0x0AB8); // custom digital pattern for blanking output
    SCIWrite(0x05, 0x0007); // delay to match adc latency
    SCIWrite(0x04, 0x0000); // switch to sensor register space


    //Load the wavetable
    SCIWriteBuf(0x7F, LUX8M_sram126Clk, 774 );


    for(int i = 0; i < LUX2100_HRES_INCREMENT; i++)
        setADCOffset(i, 0);

    SCIWrite(0x04, 0x0001); // switch to datapath register space
    SCIWrite(0x0E, 0x0001); // Enable application of ADC offsets during v blank
    SCIWrite(0x04, 0x0000); // switch back to sensor register space

    SCIWrite(0x04, 0x0000); // switch to sensor register space
*/

    //Enable timing engine
    SCIWrite(0x01, 0x0011); // enable the internal timing engine

    return SUCCESS;
}
