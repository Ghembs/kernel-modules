//#define  SDIO_HIFI

#define  CHECK_INTERVAL		150		// 100ms to check GPIO

#ifdef   SDIO_HIFI
#define  SDIO_MAJOR			150
#else
#define  SDIO_MAJOR    		140
#endif

#define  MODULE_NAME		"SDIO_Data"
#define	 DEF_BLK_SIZE		512		// SDIO block size is 512
#define  BLK_SECTOR_SIZE	2048		// sector size for addressing 256MB
//#define BLK_SECTOR_SIZE	512		// normal sector size
#define  MAX_SDIO_BLOCKS	256		// user mode could not write buffer > 256*512
#define  SRAM_BLK_SIZE		256*1024*1024	// 256MB per block
#define  MAX_SRAM_BLOCKS	SRAM_BLK_SIZE/BLK_SECTOR_SIZE
#define	 MAX_SDIO_BYTES		MAX_SDIO_BLOCKS*DEF_BLK_SIZE
#define  ADDR_GPIOB_DAT		0xb01b0014
#define  ADDR_GPIOB_INEN	0xb01b0010

enum ENUM_IOCTL_COMMAND
{
    eREAD_BYTE		=	0,		//00
    eWRITE_BYTE,				//01
    eSET_ADDRESS	=	0x80,	//80
    eREAD_REGISTER,				//81
    eWRITE_REGISTER,			//82
    eWRITE_CONFIG,		//83, for sending config data to driver
    eSET_BUFFER_SIZE,	//84
    eGET_BUFFER_SIZE,	//85
    eSET_FADE_COUNT,	//86
    eGET_FADE_COUNT,	//87
    eSET_SAMPLE_RATE,	//88
    eGET_SAMPLE_RATE,	//89
    eSET_SAMPLE_BITS,	//8a
    eGET_SAMPLE_BITS,	//8b
    eSET_MCK_FREQ,		//8c
    eGET_MCK_FREQ,		//8d
    eSET_MUTE,			//8e
    eSET_UNMUTE,		//8f
    eSET_PAUSE,			//90
    eSET_RESUME,		//91
    eSET_STOP,			//92
    eGET_SDIO_GPIO,		//93
    eGET_STATUS,		//94
    eSET_PCM_NORMAL,	//95
    eSET_PCM_INVERT,	//96
    eSET_DSD_NORMAL,	//97
    eSET_DSD_INVERT,	//98
    eSTOP_CLOCK,		//99
    eSTART_CLOCK,		//9a
    eGET_ENCODER,		//9b
    eOUTPUT_DISABLE,	//9c
    eOUTPUT_ENABLE,		//9d
    eLINEOUT_EN,		//9e
    ePHONEOUT_EN,		//9f
    eFIBER_ENABLE,		//a0
    eFIBER_DISABLE,		//a1
    eDAC_POWER_ON,		//a2
    eDAC_POWER_OFF,		//a3
    eDAC_RESET,			//a4
    eDAC_NORMAL,		//a5
    eBUFFER_A_READY,	//a6
    eBUFFER_B_READY,	//a7
    eGET_STOP,			//a8
    eGET_PAUSE,			//a9
    eSET_SEEK,			//aa
    eSET_FULL_POWER,	//ab
    eSET_OSC_FREQ,		//ac
    eEND_ENUM_IOCTL_COMMAND
};

enum eDAC_CONFIG
{
    ePCM_32000=0,
    ePCM_44100,
    ePCM_48000,
    ePCM_88200,
    ePCM_96000,
    ePCM_176400,
    ePCM_192000,
    ePCM_352800,
    ePCM_384000,
    ePCM_705600,
    ePCM_768000,
    ePCM_1411200,
    ePCM_1536000,
    eDSD_64,
    eDSD_128,
    eDSD_256,
    eDSD_512,		// not standard format
    eEND_SET_FREQ,
    eSPDIF_0,		// for dac input
    eSPDIF_1,
    eSPDIF_2,
    eSPDIF_3,
    eEND_DAC_CONFIG
};

enum eDAC_BITS
{
    ePCM_16_BIT=0,
    ePCM_24_BIT,
    ePCM_32_BIT,
    eDSD,
    eEND_DAC_BITS
};

int DAC_BIT_Config[] =
{
    0x00, 	// 16 bit
    0x40, 	// 24 bit
    0x80, 	// 32 bit
    0xc0	// dsd
};

unsigned char DAC_SR_Config[] =
{	// this is for es9018 settings
    0x21,	// 32000
    0x02,	// 44100
    0x22,	// 48000
    0x03,	// 88200
    0x23,	// 96000
    0x04,	// 176400
    0x24,	// 192000
    0x05,	// 352800
    0x25,	// 384000
    0x06,	// 705600
    0x26,	// 768000
    0x07,	// 1411200
    0x27,	// 1536000
    0x03,	// dsd64
    0x04,	// dsd128
    0x05,	// dsd256		0f
    0x06	// dsd512		10
};