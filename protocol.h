/*
* Copyright 2023 Astroasis Vision Technology, Inc. All Rights Reserved.
*/

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#define CODE_GET_PRODUCT_MODEL			0x01
#define CODE_GET_VERSION				0x02
#define CODE_GET_SERIAL_NUMBER			0x03
#define CODE_GET_FRIENDLY_NAME			0x04
#define CODE_SET_FRIENDLY_NAME			0x05
#define CODE_GET_BLUETOOTH_NAME			0x06
#define CODE_SET_BLUETOOTH_NAME			0x07
#define CODE_GET_USER_ID				0x10
#define CODE_SET_USER_ID				0x11
#define CODE_CMD_UPGRADE				0x20
#define CODE_CMD_UPGRADE_BOOTLOADER		0x21
#define CODE_GET_CONFIG					0x30
#define CODE_SET_CONFIG					0x31
#define CODE_GET_STATUS					0x32
#define CODE_CMD_FACTORY_RESET			0x33
#define CODE_SET_ZERO_POSITION			0x34
#define CODE_CMD_MOVE_STEP				0x35
#define CODE_CMD_MOVE_TO				0x36
#define CODE_CMD_STOP_MOVE				0x37
#define CODE_CMD_SYNC_POSITION			0x38
#define CODE_SET_SERIAL_NUMBER			0x39

#define UPGRADE_FRAME_DATA_LEN			32
#define MAX_FIRMWARE_LEN				(1024 * 26)

#define TEMPERATURE_ERROR				0x80000000

#ifdef __GNUC__
#define PACK(__Declaration__) typedef struct __attribute__ ((__packed__)) __Declaration__
#endif

#ifdef _MSC_VER
#define PACK(__Declaration__) __pragma(pack(push, 1)) typedef struct __Declaration__ __pragma(pack(pop))
#endif

#define FRAME_FLAG_UPGRADE_END			0x80
#define FRAME_FLAG_UPGRADE_CRC32		0x40

#define FRAME_NAME_LEN					32

PACK(_FrameHead {
	unsigned char code;
	unsigned char len;
} FrameHead);

PACK(_FrameCommandAck {
	FrameHead head;
	unsigned char result;
} FrameCommandAck);

PACK(_FrameProductModelAck {
	FrameHead head;
	unsigned char data[FRAME_NAME_LEN];
} FrameProductModelAck);

PACK(_FrameSerialNumber {
	FrameHead head;
	unsigned char data[FRAME_NAME_LEN];
} FrameSerialNumber);

PACK(_FrameVersionAck {
	FrameHead head;
	unsigned int protocal;
	unsigned int hardware;
	unsigned int firmware;
	char built[24];
} FrameVersionAck);

PACK(_FrameFriendlyName {
	FrameHead head;
	unsigned char data[FRAME_NAME_LEN];
} FrameFriendlyName);

PACK(_FrameBluetoothName {
	FrameHead head;
	unsigned char data[FRAME_NAME_LEN];
} FrameBluetoothName);

PACK(_FrameUserID {
	FrameHead head;
	unsigned int userID;
} FrameUserID);

PACK(_FrameOnOff {
	FrameHead head;
	unsigned char on;
} FrameOnOff);

PACK(_FrameMove {
	FrameHead head;
	unsigned char direction;
	unsigned int step;
} FrameMove);

PACK(_FrameMoveTo {
	FrameHead head;
	unsigned int position;
} FrameMoveTo);

PACK(_FrameSyncPosition {
	FrameHead head;
	unsigned int position;
} FrameSyncPosition);

PACK(_FrameConfig {
	FrameHead head;
	unsigned int mask;
	unsigned int maxStep;
	unsigned int backlash;
	unsigned char backlashDirection;
	unsigned char reverseDirection;
	unsigned char speed;
	unsigned char beepOnMove;
	unsigned char beepOnStartup;
	unsigned char bluetoothOn;
} FrameConfig);

PACK(_FrameStatusAck {
	FrameHead head;
	unsigned int temperatureInt;
	unsigned int temperatureExt;
	unsigned char temperatureDetection;
	unsigned char moving;
	unsigned int position;
} FrameStatusAck);

PACK(_FrameUpgrade {
	FrameHead head;
	unsigned short seq;
	unsigned char flag;
	unsigned int crc32;
	unsigned char data[UPGRADE_FRAME_DATA_LEN];
} FrameUpgrade);

PACK(_FrameUpgradeAck {
	FrameHead head;
	unsigned short seq;
	unsigned char ret;
} FrameUpgradeAck);

#define DeclareFrameHead(name, code)	FrameHead name = {code, 0}
#define DeclareFrame(type, name, code)	type name = {{code, sizeof(type) - sizeof(FrameHead)}}

#endif	/* __PROTOCOL_H__ */
