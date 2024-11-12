// SPDX-License-Identifier: GPL-2.0+
/*
 * jxh63ps1.c
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

#define SENSOR_NAME "jxh63ps1"
#define SENSOR_CHIP_ID_H (0x08)
#define SENSOR_CHIP_ID_L (0x48)
#define SENSOR_REG_END 0xff
#define SENSOR_REG_DELAY 0xfe
#define SENSOR_SUPPORT_PCLK (43200*1000)
#define SENSOR_OUTPUT_MAX_FPS 30
#define SENSOR_OUTPUT_MIN_FPS 5
#define DRIVE_CAPABILITY_1
#define SENSOR_VERSION "H20230208a"

static int reset_gpio = GPIO_PC(28);
static int pwdn_gpio = -1;
static int sensor_max_fps = TX_SENSOR_MAX_FPS_30;

struct regval_list {
    uint16_t reg_num;
    unsigned char value;
};

struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
	{0x0, 0},
	{0x1, 5731},
	{0x2, 11136},
	{0x3, 16248},
	{0x4, 21097},
	{0x5, 25710},
	{0x6, 30109},
	{0x7, 34312},
	{0x8, 38336},
	{0x9, 42195},
	{0xa, 45904},
	{0xb, 49472},
	{0xc, 52910},
	{0xd, 56228},
	{0xe, 59433},
	{0xf, 62534},
	{0x10, 65536},
	{0x11, 71267},
	{0x12, 76672},
	{0x13, 81784},
	{0x14, 86633},
	{0x15, 91246},
	{0x16, 95645},
	{0x17, 99848},
	{0x18, 103872},
	{0x19, 107731},
	{0x1a, 111440},
	{0x1b, 115008},
	{0x1c, 118446},
	{0x1d, 121764},
	{0x1e, 124969},
	{0x1f, 128070},
	{0x20, 131072},
	{0x21, 136803},
	{0x22, 142208},
	{0x23, 147320},
	{0x24, 152169},
	{0x25, 156782},
	{0x26, 161181},
	{0x27, 165384},
	{0x28, 169408},
	{0x29, 173267},
	{0x2a, 176976},
	{0x2b, 180544},
	{0x2c, 183982},
	{0x2d, 187300},
	{0x2e, 190505},
	{0x2f, 193606},
	{0x30, 196608},
	{0x31, 202339},
	{0x32, 207744},
	{0x33, 212856},
	{0x34, 217705},
	{0x35, 222318},
	{0x36, 226717},
	{0x37, 230920},
	{0x38, 234944},
	{0x39, 238803},
	{0x3a, 242512},
	{0x3b, 246080},
	{0x3c, 249518},
	{0x3d, 252836},
	{0x3e, 256041},
	{0x3f, 259142},
	{0x40, 262144},
	{0x41, 267875},
	{0x42, 273280},
	{0x43, 278392},
	{0x44, 283241},
	{0x45, 287854},
	{0x46, 292253},
	{0x47, 296456},
	{0x48, 300480},
	{0x49, 304339},
	{0x4a, 308048},
	{0x4b, 311616},
	{0x4c, 315054},
	{0x4d, 318372},
	{0x4e, 321577},
	{0x4f, 324678},
};

struct tx_isp_sensor_attribute sensor_attr;

unsigned int sensor_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again) {
		if (isp_gain == 0) {
			*sensor_again = lut->value;
			return 0;
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
	.clk = 435,
	.lans = 1,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 1280,
	.image_theight = 720,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = RAW10,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};

struct tx_isp_sensor_attribute sensor_attr={
	.name = SENSOR_NAME,
	.chip_id = 0x0848,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x40,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
	},
	.expo_fs = 1,
	.max_again = 324678,
	.max_dgain = 0,
	.total_width = 1920,
	.total_height = 900,
	.min_integration_time = 1,
	.max_integration_time = 899,
	.min_integration_time_native = 1,
	.max_integration_time_native = 899,
	.integration_time_limit = 899,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
	.one_line_expr_in_us = 44,
};

static struct regval_list sensor_init_regs_1280_720_30fps_mipi[] = {
	/* H63P(AB)_011_20221028_MIPI10_1L_1280x720x30_M24_P43P2_F1920x750_H2 */
	{0x12, 0x40},
	{0x48, 0x85},
	{0x48, 0x05},
	{0x0E, 0x11},
	{0x0F, 0x84},
	{0x10, 0x24},
	{0x11, 0x80},
	{0x57, 0x60},
	{0x58, 0x18},
	{0x61, 0x10},
	{0x46, 0x08},
	{0x0D, 0xA0},
	{0x20, 0xC0},
	{0x21, 0x03},
	{0x22, 0xEE},
	{0x23, 0x02},
	{0x24, 0x80},
	{0x25, 0xD0},
	{0x26, 0x22},
	{0x27, 0xD6},
	{0x28, 0x15},
	{0x29, 0x02},
	{0x2A, 0xCB},
	{0x2B, 0x12},
	{0x2C, 0x00},
	{0x2D, 0x00},
	{0x2E, 0xBA},
	{0x2F, 0x60},
	{0x41, 0x84},
	{0x42, 0x02},
	{0x47, 0x46},
	{0x76, 0x40},
	{0x77, 0x06},
	{0x80, 0x01},
	{0xAF, 0x22},
	{0x8A, 0x00},
	{0xA6, 0x00},
	{0xAB, 0x00},
	{0x8D, 0x49},
	{0x1D, 0x00},
	{0x1E, 0x04},
	{0x6C, 0x50},
	{0x9E, 0xF8},
	{0x6E, 0x2C},
	{0x70, 0x8C},
	{0x71, 0x6D},
	{0x72, 0x6A},
	{0x73, 0x46},
	{0x74, 0x02},
	{0x78, 0x8E},
	{0x89, 0x01},
	{0x6B, 0x20},
	{0x86, 0x40},
	{0x9C, 0xE1},
	{0x3A, 0xAC},
	{0x3B, 0x18},
	{0x3C, 0x5D},
	{0x3D, 0x80},
	{0x3E, 0x6E},
	{0x31, 0x07},
	{0x32, 0x14},
	{0x33, 0x12},
	{0x34, 0x1C},
	{0x35, 0x1C},
	{0x56, 0x12},
	{0x59, 0x20},
	{0x85, 0x14},
	{0x64, 0xD2},
	{0x8F, 0x90},
	{0xA4, 0x87},
	{0xA7, 0x80},
	{0xA9, 0x48},
	{0x45, 0x01},
	{0x5B, 0xA0},
	{0x5C, 0x6C},
	{0x5D, 0x44},
	{0x5E, 0x81},
	{0x63, 0x0F},
	{0x65, 0x12},
	{0x66, 0x43},
	{0x67, 0x79},
	{0x68, 0x00},
	{0x69, 0x78},
	{0x6A, 0x28},
	{0x7A, 0x66},
	{0xA5, 0x03},
	{0x94, 0xC0},
	{0x13, 0x81},
	{0x96, 0x84},
	{0xB7, 0x4A},
	{0x4A, 0x01},
	{0xB5, 0x0C},
	{0xA1, 0x0F},
	{0xA3, 0x40},
	{0xB1, 0x00},
	{0x93, 0x00},
	{0x7E, 0x4C},
	{0x50, 0x02},
	{0x49, 0x10},
	{0x8E, 0x40},
	{0x7F, 0x56},
	{0x0C, 0x00},
	{0xBC, 0x11},
	{0x82, 0x00},
	{0x19, 0x20},
	{0x1F, 0x10},
	{0x1B, 0x4F},
	{0x00, 0x00},
	{0x01, 0x6f},
	{0x02, 0x00},
	{0x12, 0x00},

	{SENSOR_REG_DELAY,0x10},
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_init_regs_1280_720_30fps_dvp[] = {

	{SENSOR_REG_END, 0x00},
};

/*
 * the order of the sensor_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	/* [0] 1280*720 @30fps */
	{
		.width = 1280,
		.height = 720,
		.fps = 30 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1280_720_30fps_dvp,
	},
	/* [1] 1280*720 @30fps */
	{
		.width = 1280,
		.height = 720,
		.fps = 30 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SBGGR10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1280_720_30fps_mipi,
	},
};
struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[1];


static struct regval_list sensor_stream_on[] = {
	{0x12, 0x00},
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_stream_off[] = {
	/* Sensor enter LP11*/
	{0x12, 0x40},
	{SENSOR_REG_END, 0x00},
};

int sensor_read(struct tx_isp_subdev *sd, unsigned char reg,
	       unsigned char *value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	struct i2c_msg msg[2] = {
		[0] = {
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
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

int sensor_write(struct tx_isp_subdev *sd, unsigned char reg,
		unsigned char value)
{
	int ret;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};
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
		/*pr_debug("vals->reg_num:0x%02x, vals->value:0x%02x\n",vals->reg_num, val);*/
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
		/*pr_debug("vals->reg_num:%x, vals->value:%x\n",vals->reg_num, vals->value);*/
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
	unsigned char v;
	int ret;

	ret = sensor_read(sd, 0x0a, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sensor_read(sd, 0x0b, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;

	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int sensor_set_expo(struct tx_isp_subdev* sd, int value)
{
	int ret = 0;
	int expo = (value & 0x0000ffff);
	int again = (value & 0xffff0000) >> 16;

	/*set integration time*/
	ret = sensor_write(sd, 0x01, (unsigned char)(expo & 0xff));
	ret += sensor_write(sd, 0x02, (unsigned char)((expo >> 8) & 0xff));
	/*set sensor again */
	ret += sensor_write(sd, 0x00, (unsigned char)(again & 0x7f));

	return ret;
}

#if 0
static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	unsigned int expo = value;
	int ret = 0;
	sensor_write(sd, 0x01, (unsigned char)(expo & 0xff));
	sensor_write(sd, 0x02, (unsigned char)((expo >> 8) & 0xff));
	if (ret < 0)
		return ret;

	return 0;
}
static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	/* 0x00 bit[6:0] */
	sensor_write(sd, 0x00, (unsigned char)(value & 0x7f));
	return 0;
}
#endif

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
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
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
			ret = sensor_write_array(sd, sensor_stream_on);
			sensor->video.state = TX_ISP_MODULE_RUNNING;
			pr_debug("%s stream on\n", SENSOR_NAME);
			sensor->video.state = TX_ISP_MODULE_RUNNING;
		}
	}
	else {
		ret = sensor_write_array(sd, sensor_stream_off);
		pr_debug("%s stream off\n", SENSOR_NAME);
		sensor->video.state = TX_ISP_MODULE_DEINIT;
	}
	return ret;
}

static int sensor_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int pclk = SENSOR_SUPPORT_PCLK;
	unsigned short hts;
	unsigned short vts = 0;
	unsigned char tmp;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	/* the format of fps is 16/16. for example 25 << 16 | 2, the value is 25/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (sensor_max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8))
		return -1;
	ret += sensor_read(sd, 0x21, &tmp);
	hts = tmp;
	ret += sensor_read(sd, 0x20, &tmp);
	if (ret < 0)
		return -1;
	hts = (hts << 8) + tmp;
	hts <<= 1;

	vts = pclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	sensor_write(sd, 0xc0, 0x22);
	sensor_write(sd, 0xc1, (unsigned char)(vts & 0xff));
	sensor_write(sd, 0xc2, 0x23);
	sensor_write(sd, 0xc3, (unsigned char)(vts >> 8));
	ret = sensor_read(sd, 0x1f, &tmp);
	if (ret < 0)
		return -1;

	tmp |= (1 << 7); //set bit[7],  register group write function,  auto clean
	sensor_write(sd, 0x1f, tmp);

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts-1;
	sensor->video.attr->integration_time_limit = vts-1;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts-1;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if (wsize) {
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = TISP_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		sensor->video.fps = wsize->fps;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}
	return ret;
}

static int sensor_set_hvflip(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	unsigned char val = 0;

	ret += sensor_read(sd, 0x12, &val);

	enable &= 0x03;
	switch(enable) {
	case 0:
		val = 0x00; /*normal*/
		break;
	case 1:
		val = 0x20; /*mirror*/
		break;
	case 2:
		val = 0x10; /*vflip*/
		break;
	case 3:
		val = 0x30; /*vflip & mirror*/
		break;
	default:
		break;
	}
	ret += sensor_write(sd, 0x12, val);

	return ret;
}

static int sensor_attr_check(struct tx_isp_subdev *sd)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	unsigned long rate;

	ISP_INFO("jxh63ps1: default boot is %d, video interface is %d, mclk is %d, reset is %d, pwdn is %d\n", info->default_boot, info->video_interface, info->mclk, info->rst_gpio, info->pwdn_gpio);
	switch(info->default_boot) {
	case 0:
		wsize = &sensor_win_sizes[0];
		sensor_attr.total_width = 1920;
		sensor_attr.total_height = 750;
		sensor_attr.min_integration_time = 1;
		sensor_attr.max_integration_time = 750 -1;
		sensor_attr.min_integration_time_native = 1;
		sensor_attr.max_integration_time_native = 750 - 1;
		sensor_attr.integration_time_limit = 750 - 1;
		sensor_attr.again = 0;
		sensor_attr.integration_time = 0x6f;
		pr_debug("----->dvp\n");
		break;
	case 1:
		wsize = &sensor_win_sizes[1];
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		memcpy((void*)(&(sensor_attr.mipi)),(void*)(&sensor_mipi),sizeof(sensor_mipi));
		sensor_attr.total_width = 1920;
		sensor_attr.total_height = 750;
		sensor_attr.min_integration_time = 1;
		sensor_attr.max_integration_time = 750 - 4;
		sensor_attr.min_integration_time_native = 1;
		sensor_attr.max_integration_time_native = 750 - 4;
		sensor_attr.integration_time_limit = 750 - 4;
		sensor_attr.again = 0;
		sensor_attr.integration_time = 0x6f;
		pr_debug("----->mipi\n");
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
	case TISP_SENSOR_VI_DVP:
		sensor_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP;
		break;
	default:
		ISP_ERROR("Have no this interface!!!\n");
	}

	switch(info->mclk) {
	case TISP_SENSOR_MCLK0:
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim0");
		set_sensor_mclk_function(0);
		break;
	case TISP_SENSOR_MCLK1:
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim1");
		set_sensor_mclk_function(1);
		break;
	case TISP_SENSOR_MCLK2:
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim2");
		set_sensor_mclk_function(2);
		break;
	default:
		ISP_ERROR("Have no this MCLK Source!!!\n");
	}

	rate = private_clk_get_rate(sensor->mclk);
	if (IS_ERR(sensor->mclk)) {
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	private_clk_set_rate(sensor->mclk, 24000000);
	private_clk_prepare_enable(sensor->mclk);

	reset_gpio = info->rst_gpio;
	pwdn_gpio = info->pwdn_gpio;

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
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(1);
		} else {
			ISP_ERROR("gpio request fail %d\n",reset_gpio);
		}
	}
	if (pwdn_gpio != -1) {
		ret = private_gpio_request(pwdn_gpio, "sensor_pwdn");
		if (!ret) {
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(50);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		} else {
			ISP_ERROR("gpio request fail %d\n", pwdn_gpio);
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

	if (IS_ERR_OR_NULL(sd)) {
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}

	switch(cmd) {
	case TX_ISP_EVENT_SENSOR_EXPO:
		if (arg)
			ret = sensor_set_expo(sd, sensor_val->value);
		break;
#if 0
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if (arg)
			ret = sensor_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if (arg)
			ret = sensor_set_analog_gain(sd, sensor_val->value);
		break;
#endif
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
		ret = sensor_write_array(sd, sensor_stream_off);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		ret = sensor_write_array(sd, sensor_stream_on);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if (arg)
			ret = sensor_set_fps(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if (arg)
			ret = sensor_set_hvflip(sd, sensor_val->value);
		break;
	default:
		break;
	}
	return 0;
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
	/*.ioctl = sensor_ops_ioctl,*/
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

static int sensor_probe(struct i2c_client *client, const struct i2c_device_id *id)
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

	sensor_attr.expo_fs = 1;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->dev = &client->dev;
	sensor->video.shvflip = 1;
	sensor->video.attr = &sensor_attr;
	sensor->video.mbus_change = 0;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
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
