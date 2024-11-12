// SPDX-License-Identifier: GPL-2.0+
/*
 * sc031gs.c
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <tx-isp-common.h>
#include <sensor-common.h>

#define SENSOR_NAME "sc031gs"
#define SENSOR_CHIP_ID_H (0x00)
#define SENSOR_CHIP_ID_L (0x31)
#define SENSOR_REG_END 0xffff
#define SENSOR_REG_DELAY 0xfffe
#define SENSOR_SUPPORT_120FPS_SCLK (71960880)
#define SENSOR_OUTPUT_MAX_FPS 180
#define SENSOR_OUTPUT_MIN_FPS 5
#define DRIVE_CAPABILITY_1
#define MCLK 24000000
#define SENSOR_VERSION "H20221012a"

static int reset_gpio = -1;
static int pwdn_gpio = -1;

char* __attribute__((weak)) sclk_name[4];

struct regval_list {
    uint16_t reg_num;
    unsigned char value;
};

struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
	{ 0x10, 0 },
	{ 0x11, 5732 },
	{ 0x12, 11137 },
	{ 0x13, 16248 },
	{ 0x14, 21098 },
	{ 0x15, 25711 },
	{ 0x16, 30109 },
	{ 0x17, 34312 },
	{ 0x18, 38336 },
	{ 0x19, 42196 },
	{ 0x1a, 45904 },
	{ 0x1b, 49472 },
	{ 0x1c, 52911 },
	{ 0x1d, 56228 },
	{ 0x1e, 59434 },
	{ 0x1f, 62534 },
	{ 0x110, 65535 },
	{ 0x111, 71267 },
	{ 0x112, 76672 },
	{ 0x113, 81783 },
	{ 0x114, 86633 },
	{ 0x115, 91246 },
	{ 0x116, 95644 },
	{ 0x117, 99847 },
	{ 0x118, 103871 },
	{ 0x119, 107731 },
	{ 0x11a, 111439 },
	{ 0x11b, 115007 },
	{ 0x11c, 118446 },
	{ 0x11d, 121763 },
	{ 0x11e, 124969 },
	{ 0x11f, 128069 },
	{ 0x310, 131070 },
	{ 0x311, 136802 },
	{ 0x312, 142207 },
	{ 0x313, 147318 },
	{ 0x314, 152168 },
	{ 0x315, 156781 },
	{ 0x316, 161179 },
	{ 0x317, 165382 },
	{ 0x318, 169406 },
	{ 0x319, 173266 },
	{ 0x31a, 176974 },
	{ 0x31b, 180542 },
	{ 0x31c, 183980 },
	{ 0x31d, 187298 },
	{ 0x31e, 190504 },
	{ 0x31f, 193604 },
	{ 0x710, 196605 },
	{ 0x711, 202337 },
	{ 0x712, 207742 },
	{ 0x713, 212853 },
	{ 0x714, 217703 },
	{ 0x715, 222316 },
	{ 0x716, 226714 },
	{ 0x717, 230917 },
	{ 0x718, 234941 },
	{ 0x719, 238801 },
	{ 0x71a, 242509 },
	{ 0x71b, 246077 },
	{ 0x71c, 249515 },
	{ 0x71d, 252833 },
	{ 0x71e, 256039 },
};

struct tx_isp_sensor_attribute sensor_attr;

unsigned int sensor_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again) {
		if (isp_gain == 0) {
			*sensor_again = lut[0].value;
			return lut[0].gain;
		}
		else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		}
		else {
			if ((lut->gain == sensor_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int sensor_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_mipi_bus sensor_mipi={
   .mode = SENSOR_MIPI_OTHER_MODE,
   .clk = 480,
   .lans = 1,
   .settle_time_apative_en = 0,
   .mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
   .mipi_sc.hcrop_diff_en = 0,
   .mipi_sc.mipi_vcomp_en = 0,
   .mipi_sc.mipi_hcomp_en = 0,
   .mipi_sc.line_sync_mode = 0,
   .mipi_sc.work_start_flag = 0,
   .image_twidth = 640,
   .image_theight = 480,
   .mipi_sc.mipi_crop_start0x = 0,
   .mipi_sc.mipi_crop_start0y = 0,
   .mipi_sc.mipi_crop_start1x = 0,
   .mipi_sc.mipi_crop_start1y = 0,
   .mipi_sc.mipi_crop_start2x = 0,
   .mipi_sc.mipi_crop_start2y = 0,
   .mipi_sc.mipi_crop_start3x = 0,
   .mipi_sc.mipi_crop_start3y = 0,
   .mipi_sc.data_type_en = 0,
   .mipi_sc.data_type_value = RAW10,
   .mipi_sc.del_start = 0,
   .mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
   .mipi_sc.sensor_fid_mode = 0,
   .mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};
struct tx_isp_mipi_bus sensor_mipi_ ={
   .mode = SENSOR_MIPI_OTHER_MODE,
   .clk = 720,
   .lans = 1,
   .settle_time_apative_en = 0,
   .mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
   .mipi_sc.hcrop_diff_en = 0,
   .mipi_sc.mipi_vcomp_en = 0,
   .mipi_sc.mipi_hcomp_en = 0,
   .mipi_sc.line_sync_mode = 0,
   .mipi_sc.work_start_flag = 0,
   .image_twidth = 640,
   .image_theight = 240,
   .mipi_sc.mipi_crop_start0x = 0,
   .mipi_sc.mipi_crop_start0y = 0,
   .mipi_sc.mipi_crop_start1x = 0,
   .mipi_sc.mipi_crop_start1y = 0,
   .mipi_sc.mipi_crop_start2x = 0,
   .mipi_sc.mipi_crop_start2y = 0,
   .mipi_sc.mipi_crop_start3x = 0,
   .mipi_sc.mipi_crop_start3y = 0,
   .mipi_sc.data_type_en = 0,
   .mipi_sc.data_type_value = RAW10,
   .mipi_sc.del_start = 0,
   .mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
   .mipi_sc.sensor_fid_mode = 0,
   .mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};
struct tx_isp_mipi_bus sensor_mipi_ ={
   .mode = SENSOR_MIPI_OTHER_MODE,
   .clk = 720,
   .lans = 1,
   .settle_time_apative_en = 0,
   .mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
   .mipi_sc.hcrop_diff_en = 0,
   .mipi_sc.mipi_vcomp_en = 0,
   .mipi_sc.mipi_hcomp_en = 0,
   .mipi_sc.line_sync_mode = 0,
   .mipi_sc.work_start_flag = 0,
   .image_twidth = 640,
   .image_theight = 240,
   .mipi_sc.mipi_crop_start0x = 0,
   .mipi_sc.mipi_crop_start0y = 0,
   .mipi_sc.mipi_crop_start1x = 0,
   .mipi_sc.mipi_crop_start1y = 0,
   .mipi_sc.mipi_crop_start2x = 0,
   .mipi_sc.mipi_crop_start2y = 0,
   .mipi_sc.mipi_crop_start3x = 0,
   .mipi_sc.mipi_crop_start3y = 0,
   .mipi_sc.data_type_en = 0,
   .mipi_sc.data_type_value = RAW10,
   .mipi_sc.del_start = 0,
   .mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
   .mipi_sc.sensor_fid_mode = 0,
   .mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};
struct tx_isp_sensor_attribute sensor_attr={
	.name = SENSOR_NAME,
	.chip_id = 0x031,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_16BITS,
	.cbus_device = 0x30,
	.data_type = TX_SENSOR_DATA_TYPE_LINEAR,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.max_again = 256039,
	.max_dgain = 0,
	.min_integration_time = 6,
	.min_integration_time_native = 6,
	.max_integration_time_native = 0x2ab - 6,
	.integration_time_limit = 0x2ab - 6,
	.total_width = 0x36e,
	.total_height = 0x2ab,
	.max_integration_time = 0x2ab - 6,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
};

static struct regval_list sensor_init_regs_640_480_120fps_mipi[] = {
	{0x0103,0x01},
	{0x0100,0x00},
	{0x3000,0x00},
	{0x3001,0x00},
	{0x300f,0x0f},
	{0x3018,0x13},
	{0x3019,0xfe},
	{0x301c,0x78},
	{0x3031,0x0a},
	{0x3037,0x20},
	{0x303f,0x01},
	{0x320c,0x03},// hts
	{0x320d,0x6e},//
	{0x320e,0x02},// vts
	{0x320f,0xab},//
	{0x3220,0x10},
	{0x3250,0xc0},
	{0x3251,0x02},
	{0x3252,0x02},
	{0x3253,0xa6},
	{0x3254,0x02},
	{0x3255,0x07},
	{0x3304,0x48},
	{0x3306,0x38},
	{0x3309,0x68},
	{0x330b,0xe0},
	{0x330c,0x18},
	{0x330f,0x20},
	{0x3310,0x10},
	{0x3314,0x1e},
	{0x3315,0x38},
	{0x3316,0x40},
	{0x3317,0x10},
	{0x3329,0x34},
	{0x332d,0x34},
	{0x332f,0x38},
	{0x3335,0x3c},
	{0x3344,0x3c},
	{0x335b,0x80},
	{0x335f,0x80},
	{0x3366,0x06},
	{0x3385,0x31},
	{0x3387,0x51},
	{0x3389,0x01},
	{0x33b1,0x03},
	{0x33b2,0x06},
	{0x3621,0xa4},
	{0x3622,0x05},
	{0x3624,0x47},
	{0x3630,0x46},
	{0x3631,0x48},
	{0x3633,0x52},
	{0x3635,0x18},
	{0x3636,0x25},
	{0x3637,0x89},
	{0x3638,0x0f},
	{0x3639,0x08},
	{0x363a,0x00},
	{0x363b,0x48},
	{0x363c,0x06},
	{0x363d,0x00},
	{0x363e,0xf8},
	{0x3640,0x00},
	{0x3641,0x01},  //驱动能力调节，0x02 /0x03
	{0x36e9,0x00},
	{0x36ea,0x3b},
	{0x36eb,0x0e},
	{0x36ec,0x0e},
	{0x36ed,0x33},
	{0x36f9,0x00},
	{0x36fa,0x3a},
	{0x36fc,0x01},
	{0x3908,0x91},
	{0x3d08,0x01},
#if 1
	{0x3e00,0x00},
	//{0x3e01,0x2a},//曝光
	//{0x3e02,0x50},
	{0x3e01,0x0F},//曝光
	{0x3e02,0xA0},
	{0x3e06,0x0c},
	//{0x3e08,0x00},//模拟增益
	//{0x3e09,0x16},
	{0x3e08,0x04},//模拟增益3
	{0x3e09,0x18},
#endif
#if 0
	{0x3e01,0x14},
	{0x3e02,0x80},
	{0x3e06,0x0c},
#endif

	{0x4500,0x59},
	{0x4501,0xc4},
	{0x4603,0x00},
	{0x4809,0x01},
	{0x4837,0x1b},
	{0x5011,0x00},
	{0x0100,0x01},
	{SENSOR_REG_DELAY, 0x10},
	{0x4418,0x08},
	{0x4419,0x8e},
	{SENSOR_REG_END, 0x00},
};



static struct regval_list sensor_init_regs_640_240_180fps_mipi[] = {
	{0x0103,0x01},
	{0x0100,0x00},
	{0x36e9,0x80},
	{0x36f9,0x80},
	{0x3001,0x00},
	{0x3000,0x00},
	{0x300f,0x0f},
	{0x3018,0x13},
	{0x3019,0xfe},
	{0x301c,0x78},
	{0x301f,0x69},
	{0x3031,0x0a},
	{0x3037,0x20},
	{0x303f,0x01},
	{0x320a,0x00},
	{0x320b,0xf0},
	{0x320c,0x03},
	{0x320d,0x6e},
	{0x320e,0x01},
	{0x320f,0xc7},
	{0x3213,0x04},
	{0x3215,0x22},
	{0x3220,0x10},
	{0x3250,0xc0},
	{0x3251,0x02},
	{0x3252,0x01},
	{0x3253,0xc2},
	{0x3254,0x02},
	{0x3255,0x07},
	{0x3304,0x48},
	{0x3306,0x38},
	{0x3309,0x68},
	{0x330b,0xe0},
	{0x330c,0x18},
	{0x330f,0x20},
	{0x3310,0x10},
	{0x3314,0x1e},
	{0x3315,0x38},
	{0x3316,0x40},
	{0x3317,0x10},
	{0x3329,0x34},
	{0x332d,0x34},
	{0x332f,0x38},
	{0x3335,0x3c},
	{0x3344,0x3c},
	{0x335b,0x80},
	{0x335f,0x80},
	{0x3366,0x06},
	{0x3385,0x31},
	{0x3387,0x51},
	{0x3389,0x01},
	{0x33b1,0x03},
	{0x33b2,0x06},
	{0x3621,0xa4},
	{0x3622,0x05},
	{0x3624,0x47},
	{0x3630,0x46},
	{0x3631,0x48},
	{0x3633,0x52},
	{0x3635,0x18},
	{0x3636,0x25},
	{0x3637,0x89},
	{0x3638,0x0f},
	{0x3639,0x08},
	{0x363a,0x00},
	{0x363b,0x48},
	{0x363c,0x06},
	{0x363d,0x00},
	{0x363e,0xf8},
	{0x3640,0x00},
	{0x3641,0x01},
	{0x36ea,0x3b},
	{0x36eb,0x0e},
	{0x36ec,0x0e},
	{0x36ed,0x33},
	{0x36fa,0x3a},
	{0x36fc,0x01},
	{0x3908,0x91},
	{0x3d08,0x01},
	{0x3e01,0x14},
	{0x3e02,0x80},
	{0x3e06,0x0c},
	{0x3f04,0x03},
	{0x3f05,0x4e},
	{0x4500,0x59},
	{0x4501,0xc4},
	{0x4603,0x00},
	{0x4809,0x01},
	{0x4837,0x1b},
	{0x5011,0x00},
	{0x36e9,0x00},
	{0x36f9,0x00},
	{0x0100,0x01},
	{0x4418,0x08},
	{0x4419,0x8e},
	{SENSOR_REG_END, 0x00},
};
static struct regval_list sensor_init_regs_640_240_150fps_mipi[] = {
	{0x0103,0x01},
	{0x0100,0x00},
	{0x36e9,0x80},
	{0x36f9,0x80},
	{0x3001,0x00},
	{0x3000,0x00},
	{0x300f,0x0f},
	{0x3018,0x13},
	{0x3019,0xfe},
	{0x301c,0x78},
	{0x301f,0x86},
	{0x3031,0x0a},
	{0x3037,0x20},
	{0x303f,0x01},
	{0x320a,0x00},
	{0x320b,0xf0},
	{0x320c,0x03},
	{0x320d,0x20},
	{0x320e,0x02},
	{0x320f,0x58},
	{0x3213,0x04},
	{0x3215,0x22},
	{0x3220,0x10},
	{0x3250,0xc0},
	{0x3251,0x02},
	{0x3252,0x02},
	{0x3253,0x53},
	{0x3254,0x02},
	{0x3255,0x07},
	{0x3301,0x31},
	{0x3304,0x48},
	{0x3306,0x38},
	{0x3309,0x68},
	{0x330b,0xa8},
	{0x330c,0x18},
	{0x330f,0x20},
	{0x3310,0x10},
	{0x3314,0x1e},
	{0x3315,0x38},
	{0x3316,0x40},
	{0x3317,0x10},
	{0x3329,0x34},
	{0x332d,0x34},
	{0x332f,0x38},
	{0x3335,0x3c},
	{0x3344,0x3c},
	{0x335b,0x80},
	{0x335f,0x80},
	{0x3366,0x06},
	{0x3385,0x31},
	{0x3387,0x51},
	{0x3389,0x01},
	{0x33b1,0x03},
	{0x33b2,0x06},
	{0x3621,0xa4},
	{0x3622,0x05},
	{0x3624,0x47},
	{0x3630,0x46},
	{0x3631,0x48},
	{0x3633,0x52},
	{0x3635,0x18},
	{0x3636,0x25},
	{0x3637,0x89},
	{0x3638,0x0f},
	{0x3639,0x08},
	{0x363a,0x00},
	{0x363b,0x48},
	{0x363c,0x06},
	{0x363d,0x00},
	{0x363e,0xf8},
	{0x3640,0x00},
	{0x3641,0x01},
	{0x36ea,0x3b},
	{0x36eb,0x0e},
	{0x36ec,0x0e},
	{0x36ed,0x33},
	{0x36fa,0x3a},
	{0x36fc,0x01},
	{0x3908,0x91},
	{0x3d08,0x01},
	{0x3e01,0x25},
	{0x3e02,0x20},
	{0x3e06,0x0c},
	{0x3f04,0x03},
	{0x3f05,0x00},
	{0x4500,0x59},
	{0x4501,0xc4},
	{0x4603,0x00},
	{0x4809,0x01},
	{0x4837,0x1b},
	{0x5011,0x00},
	{0x36e9,0x00},
	{0x36f9,0x00},
	{0x0100,0x01},
	{0x4418,0x08},
	{0x4419,0x8e},

	{SENSOR_REG_END, 0x00},
};
static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	/* 640*480 */
	{
		.width = 640,
		.height = 480,
		.fps = 120 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_640_480_120fps_mipi,
	},
	/* 640*240 */
	{
		.width = 640,
		.height = 240,
		.fps = 180 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_640_240_180fps_mipi,
	}
	/* 640*240 */
	{
		.width = 640,
		.height = 240,
		.fps = 150 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_640_240_150fps_mipi,
	}
};
struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[0];


static struct regval_list sensor_stream_on_mipi[] = {
	{0x0100, 0x01},
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_stream_off_mipi[] = {
	{0x0100, 0x00},
	{SENSOR_REG_END, 0x00},
};

int sensor_read(struct tx_isp_subdev *sd, uint16_t reg,
		unsigned char *value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[2] = {(reg >> 8) & 0xff, reg & 0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = buf,
		},
		[1] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = value,
		}
	};
	int ret;
	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int sensor_write(struct tx_isp_subdev *sd, uint16_t reg,
		 unsigned char value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[3] = {(reg >> 8) & 0xff, reg & 0xff, value};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 3,
		.buf = buf,
	};
	int ret;
	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

#if 0
static int sensor_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = sensor_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}
#endif

static int sensor_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = sensor_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int sensor_reset(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	return 0;
}

static int sensor_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	int ret;
	unsigned char v;

	ret = sensor_read(sd, 0x3107, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sensor_read(sd, 0x3108, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

#if 1
static int sensor_set_expo(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	int it = (value & 0xffff);
	int again = (value & 0xffff0000) >> 16;

	ret += sensor_write(sd, 0x3e00, (unsigned char)((it >> 12) & 0xf));
	ret += sensor_write(sd, 0x3e01, (unsigned char)((it >> 4) & 0xff));
	ret += sensor_write(sd, 0x3e02, (unsigned char)((it & 0x0f) << 4));
    ret = sensor_write(sd, 0x3e09, (unsigned char)(again & 0xff));
	ret += sensor_write(sd, 0x3e08, (unsigned char)(((again >> 8) & 0xff)));
	if (ret < 0)
		return ret;
	return 0;
}
#endif

#if 0
static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

//	printk("---------[%s]:%d--------------\n",__func__,__LINE__);
	value *= 1;
	ret = sensor_write(sd, 0x3e00, (unsigned char)((value >> 12) & 0x0f));
	ret += sensor_write(sd, 0x3e01, (unsigned char)((value >> 4) & 0xff));
	ret += sensor_write(sd, 0x3e02, (unsigned char)((value & 0x0f) << 4));
	if (ret < 0)
		return ret;

	return 0;
}

static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

//	printk("---------[%s]:%d--------------\n",__func__,__LINE__);
	ret += sensor_write(sd, 0x3e09, (unsigned char)(value & 0xff));
	ret += sensor_write(sd, 0x3e08, (unsigned char)((value & 0xff00) >> 8));
	if (ret < 0)
		return ret;

	return 0;
}
#endif

static int sensor_set_attr(struct tx_isp_subdev *sd, struct tx_isp_sensor_win_setting *wise)
{
    struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

    sensor->video.vi_max_width = wsize->width;
    sensor->video.vi_max_height = wsize->height;
    sensor->video.mbus.width = wsize->width;
    sensor->video.mbus.height = wsize->height;
    sensor->video.mbus.code = wsize->mbus_code;
    sensor->video.mbus.field = TISP_FIELD_NONE;
    sensor->video.mbus.colorspace = wsize->colorspace;
    sensor->video.fps = wsize->fps;

    return 0;

}

static int sensor_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_init(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if (!init->enable)
		return ISP_SUCCESS;

	sensor_set_attr(sd, wsize);
	sensor->video.state = TX_ISP_MODULE_DEINIT;

	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int sensor_s_stream(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	int ret = 0;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	if (init->enable) {
		if (sensor->video.state == TX_ISP_MODULE_DEINIT) {
			ret = sensor_write_array(sd, wsize->regs);
			if (ret)
				return ret;
			sensor->video.state = TX_ISP_MODULE_INIT;
		}
		if (sensor->video.state == TX_ISP_MODULE_INIT) {
			ret = sensor_write_array(sd, sensor_stream_on_mipi);
			ISP_WARNING("%s stream on\n", SENSOR_NAME);
			sensor->video.state = TX_ISP_MODULE_RUNNING;
		}
	}
	else {
		ret = sensor_write_array(sd, sensor_stream_off_mipi);
		ISP_WARNING("%s stream off\n", SENSOR_NAME);
		sensor->video.state = TX_ISP_MODULE_DEINIT;
	}

	return ret;
}

static int sensor_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned char val = 0;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%d) not in range\n", fps);
		return -1;
	}
	sclk = SENSOR_SUPPORT_120FPS_SCLK;

	ret += sensor_read(sd, 0x320c, &val);
	hts = val << 8;
	ret += sensor_read(sd, 0x320d, &val);
	hts = (hts | val);
	if (0 != ret) {
		ISP_ERROR("Error: %s read error\n", SENSOR_NAME);
		return -1;
	}

	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);

	ret = sensor_write(sd, 0x320f, (unsigned char)(vts & 0xff));
	ret += sensor_write(sd, 0x320e, (unsigned char)(vts >> 8));
	if (0 != ret) {
		ISP_ERROR("Error: %s write error\n", SENSOR_NAME);
		return ret;
	}
	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 4;
	sensor->video.attr->integration_time_limit = vts - 4;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 4;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if (wsize) {
		sensor_set_attr(sd, wsize);
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}

	return ret;
}

struct clk *sclka;
static int sensor_attr_check(struct tx_isp_subdev *sd)
{
	unsigned long rate;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);

	switch(info->default_boot) {
	case 0:
	memcpy(&(sensor_attr.mipi), &sensor_mipi, sizeof(sensor_mipi));
		break;
	case 1:
		wsize = &sensor_win_sizes[1];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy((void*)(&(sensor_attr.mipi)),(void*)(&sensor_mipi_1),sizeof(sensor_mipi_1));
		sensor_attr.mipi.clk = 720;
		sensor_attr.max_integration_time_native = 0x1c7 - 6;
		sensor_attr.integration_time_limit = 0x1c7 - 6;
		sensor_attr.total_width = 0x36e;
		sensor_attr.total_height = 0x1c7;
		sensor_attr.max_integration_time = 0x1c7 - 6;
		sensor_attr.one_line_expr_in_us = 28;
		break;
	case 2:
		wsize = &sensor_win_sizes[2];
		sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy((void*)(&(sensor_attr.mipi)),(void*)(&sensor_mipi_2),sizeof(sensor_mipi_2));
		sensor_attr.mipi.clk = 720;
		sensor_attr.max_integration_time_native = 0x258 - 6;
		sensor_attr.integration_time_limit = 0x258 - 6;
		sensor_attr.total_width = 0x320;
		sensor_attr.total_height = 0x258;
		sensor_attr.max_integration_time = 0x258 - 6;
		sensor_attr.one_line_expr_in_us = 28;
		break;
	default:
		ISP_ERROR("Have no this setting!!!\n");
	}

	switch(info->video_interface) {
	case TISP_SENSOR_VI_MIPI_CSI0:
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		sensor_attr.mipi.index = 0;
		break;
	case TISP_SENSOR_VI_MIPI_CSI1:
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		sensor_attr.mipi.index = 1;
		break;
	default:
		ISP_ERROR("Have no this interface!!!\n");
	}

	switch(info->mclk) {
	case TISP_SENSOR_MCLK0:
		sclka = private_devm_clk_get(&client->dev, "mux_cim0");
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim0");
		set_sensor_mclk_function(0);
		break;
	case TISP_SENSOR_MCLK1:
		sclka = private_devm_clk_get(&client->dev, "mux_cim1");
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim1");
		set_sensor_mclk_function(1);
		break;
	case TISP_SENSOR_MCLK2:
		sclka = private_devm_clk_get(&client->dev, "mux_cim2");
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim2");
		set_sensor_mclk_function(2);
		break;
	default:
		ISP_ERROR("Have no this MCLK Source!!!\n");
	}

	if (IS_ERR(sensor->mclk)) {
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	rate = private_clk_get_rate(sensor->mclk);

	private_clk_set_rate(sensor->mclk, MCLK);
	private_clk_prepare_enable(sensor->mclk);

	reset_gpio = info->rst_gpio;
	pwdn_gpio = info->pwdn_gpio;

	sensor_set_attr(sd, wsize);
	sensor->priv = wsize;

	return 0;

err_get_mclk:
	return -1;
}

static int sensor_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;
	sensor_attr_check(sd);
	if (reset_gpio != -1) {
		ret = private_gpio_request(reset_gpio,"sensor_reset");
		if (!ret) {
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(10);
		} else {
			ISP_ERROR("gpio request fail %d\n",reset_gpio);
		}
	}
	if (pwdn_gpio != -1) {
		ret = private_gpio_request(pwdn_gpio,"sensor_pwdn");
		if (!ret) {
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(10);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		} else {
			ISP_ERROR("gpio request fail %d\n",pwdn_gpio);
		}
	}
	ret = sensor_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an %s chip.\n",
			  client->addr, client->adapter->name, SENSOR_NAME);
		return ret;
	}
	ISP_WARNING("%s chip found @ 0x%02x (%s)\n",
		    SENSOR_NAME, client->addr, client->adapter->name);
	ISP_WARNING("sensor driver version %s\n",SENSOR_VERSION);
	if (chip) {
		memcpy(chip->name, SENSOR_NAME, sizeof(SENSOR_NAME));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}

	return 0;
}

static int sensor_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	struct tx_isp_sensor_value *sensor_val = arg;
	return 0;

	if (IS_ERR_OR_NULL(sd)) {
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd) {
	case TX_ISP_EVENT_SENSOR_EXPO:
		if (arg)
			ret = sensor_set_expo(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME:
//		if (arg)
//			ret = sensor_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
//		if (arg)
//			ret = sensor_set_analog_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if (arg)
			ret = sensor_set_digital_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if (arg)
			ret = sensor_get_black_pedestal(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if (arg)
			ret = sensor_set_mode(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_off_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_on_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if (arg)
			ret = sensor_set_fps(sd, sensor_val->value);
		break;
	default:
		break;
	}

	return ret;
}

static int sensor_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
{
	unsigned char val = 0;
	int len = 0;
	int ret = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len)) {
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = sensor_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int sensor_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len)) {
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	sensor_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops sensor_core_ops = {
	.g_chip_ident = sensor_g_chip_ident,
	.reset = sensor_reset,
	.init = sensor_init,
	.g_register = sensor_g_register,
	.s_register = sensor_s_register,
};

static struct tx_isp_subdev_video_ops sensor_video_ops = {
	.s_stream = sensor_s_stream,
};

static struct tx_isp_subdev_sensor_ops sensor_sensor_ops = {
	.ioctl = sensor_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.sensor = &sensor_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = SENSOR_NAME,
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};


static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));

	sd = &sensor->sd;
	video = &sensor->video;
	sensor->dev = &client->dev;
	sensor_attr.expo_fs = 1;
	sensor->video.attr = &sensor_attr;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sensor_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->%s\n", SENSOR_NAME);

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if (reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if (pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);

	private_clk_disable_unprepare(sensor->mclk);
	private_devm_clk_put(&client->dev, sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = SENSOR_NAME,
	},
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};

static __init int init_sensor(void)
{
	return private_i2c_add_driver(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	private_i2c_del_driver(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

MODULE_DESCRIPTION("A low-level driver for "SENSOR_NAME" sensor");
MODULE_LICENSE("GPL");
