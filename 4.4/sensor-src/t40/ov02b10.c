// SPDX-License-Identifier: GPL-2.0+
/*
 * ov02b10.c
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 *
 * Settings:
 * sboot        resolution      fps       interface              mode
 *   0          1600*1200       15        mipi_1lane           linear
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <soc/gpio.h>
#include <tx-isp-common.h>
#include <sensor-common.h>

#define SENSOR_NAME "ov02b10"
#define SENSOR_CHIP_ID_H (0x00)
#define SENSOR_CHIP_ID_L (0x2b)
#define SENSOR_REG_END 0xff
#define SENSOR_REG_PAGE 0xfd
#define SENSOR_REG_DELAY 0xfe
#define SENSOR_SUPPORT_SCLK_FPS_15 (16490880)
#define SENSOR_OUTPUT_MAX_FPS 15
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION "H20230725a"

static int reset_gpio = GPIO_PC(27);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int shvflip = 1;
module_param(shvflip, int, S_IRUGO);
MODULE_PARM_DESC(shvflip, "Sensor HV Flip Enable interface");

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
	{0x10, 0},
	{0x11, 5687},
	{0x12, 11136},
	{0x13, 16208},
	{0x14, 21097},
	{0x15, 25674},
	{0x16, 30109},
	{0x17, 34279},
	{0x18, 38336},
	{0x19, 42165},
	{0x1a, 45904},
	{0x1b, 49444},
	{0x1c, 52910},
	{0x1d, 56202},
	{0x1e, 59433},
	{0x1f, 62509},
	{0x20, 65536},
	{0x22, 71267},
	{0x24, 76672},
	{0x26, 81784},
	{0x28, 86633},
	{0x2a, 91246},
	{0x2c, 95645},
	{0x2e, 99848},
	{0x30, 103872},
	{0x32, 107731},
	{0x34, 111440},
	{0x36, 115008},
	{0x38, 118446},
	{0x3a, 121764},
	{0x3c, 124969},
	{0x3e, 128070},
	{0x40, 131072},
	{0x44, 136803},
	{0x48, 142208},
	{0x4c, 147320},
	{0x50, 152169},
	{0x54, 156782},
	{0x58, 161181},
	{0x5c, 165384},
	{0x60, 169408},
	{0x64, 173267},
	{0x68, 176976},
	{0x6c, 180544},
	{0x70, 183982},
	{0x74, 187300},
	{0x78, 190505},
	{0x7c, 193606},
	{0x80, 196608},
	{0x88, 202339},
	{0x90, 207744},
	{0x98, 212856},
	{0xa0, 217705},
	{0xa8, 222318},
	{0xb0, 226717},
	{0xb8, 230920},
	{0xc0, 234944},
	{0xc8, 238803},
	{0xd0, 242512},
	{0xd8, 246080},
	{0xe0, 249518},
	{0xe8, 252836},
	{0xf0, 256041},
	{0xf8, 259142},
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

struct tx_isp_sensor_attribute sensor_attr={
	.name = SENSOR_NAME,
	.chip_id = 0x002b,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x3c,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.mipi = {
		.mode = SENSOR_MIPI_OTHER_MODE,
		.clk = 660,
		.lans = 1,
		.settle_time_apative_en = 1,
		.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
		.mipi_sc.hcrop_diff_en = 0,
		.mipi_sc.mipi_vcomp_en = 0,
		.mipi_sc.mipi_hcomp_en = 0,
		.image_twidth = 1600,
		.image_theight = 1200,
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
	},
	.max_again = 259142,
	.max_dgain = 0,
	.min_integration_time = 1,
	.min_integration_time_native = 1,
	.max_integration_time_native = 2447,
	.integration_time_limit = 2447,
	.total_width = 448,
	.total_height = 2454,
	.max_integration_time = 2447,
	.one_line_expr_in_us = 22,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
};

static struct regval_list sensor_init_regs_1600_1200_15fps[] = {
#if 0
	{0xfd,0x00},//P0
	{0x24,0x02},
	{0x25,0x06},
	{0x29,0x01},
	{0x2a,0xb4},
	{0x2b,0x00},
	{0x1e,0x17},
	{0x33,0x07},
	{0x35,0x07},
	{0x4a,0x0c},
	{0x3a,0x05},
	{0x3b,0x02},
	{0x3e,0x00},
	{0x46,0x01},
	{0x6d,0x03},
	{0xfd,0x01},//P1
	{0x0e,0x02},
	{0x0f,0x1a},
	{0x18,0x00},
	{0x22,0xff},
	{0x23,0x02},
	{0x17,0x2c},
	{0x19,0x20},
	{0x1b,0x06},
	{0x1c,0x04},
	{0x20,0x03},
	{0x30,0x01},
	{0x33,0x01},
	{0x31,0x0a},
	{0x32,0x09},
	{0x38,0x01},
	{0x39,0x01},
	{0x3a,0x01},
	{0x3b,0x01},
	{0x4f,0x04},
	{0x4e,0x05},
	{0x50,0x01},
	{0x35,0x0c},
	{0x45,0x2a},
	{0x46,0x2a},
	{0x47,0x2a},
	{0x48,0x2a},
	{0x4a,0x2c},
	{0x4b,0x2c},
	{0x4c,0x2c},
	{0x4d,0x2c},
	{0x56,0x3a},
	{0x57,0x0a},
	{0x58,0x24},
	{0x59,0x20},
	{0x5a,0x0a},
	{0x5b,0xff},
	{0x37,0x0a},
	{0x42,0x0e},
	{0x68,0x90},
	{0x69,0xcd},
	{0x6a,0x8f},
	{0x7c,0x0a},
	{0x7d,0x09},
	{0x7e,0x09},
	{0x7f,0x08},
	{0x83,0x14},
	{0x84,0x14},
	{0x86,0x14},
	{0x87,0x07},
	{0x88,0x0f},
	{0x94,0x02},
	{0x98,0xd1},
	{0xfe,0x02},
	{0xfd,0x03},//P3
	{0x97,0x78},
	{0x98,0x78},
	{0x99,0x78},
	{0x9a,0x78},
	{0xa1,0x40},
	{0xb1,0x30},
	{0xae,0x0d},
	{0x88,0x5b},
	{0x89,0x7c},
	{0xb4,0x05},
	{0x8c,0x40},
	{0x8e,0x40},
	{0x90,0x40},
	{0x92,0x40},
	{0x9b,0x46},
	{0xac,0x40},
	{0xfd,0x00},//P0
	{0x5a,0x15},
	{0x74,0x01},
	{0xfd,0x00},//P0
	{0x50,0x40},
	{0x52,0xb0},
	{0xfd,0x01},//P1
	{0x03,0x70},
	{0x05,0x10},
	{0x07,0x20},
	{0x09,0xb0},
	{0xfd,0x01},//P1
	{0x14,0x04},
	{0x15,0xd2},
	{0xfb,0x01},
	{0xfd,0x03},//P3
	{0xc2,0x01},
	{0xfd,0x01},//P1
#else
	{0xfd,0x00},
	{0x24,0x02},
	{0x25,0x06},
	{0x29,0x01},
	{0x2a,0xb4},
	{0x2b,0x00},
	{0x1e,0x17},
	{0x33,0x07},
	{0x35,0x07},
	{0x4a,0x0c},
	{0x3a,0x05},
	{0x3b,0x02},
	{0x3e,0x00},
	{0x46,0x01},
	{0x6d,0x03},
	{0xfd,0x01},
	{0x0e,0x02},
	{0x0f,0x1a},
	{0x18,0x00},
	{0x22,0xff},
	{0x23,0x02},
	{0x17,0x2c},
	{0x19,0x20},
	{0x1b,0x06},
	{0x1c,0x04},
	{0x20,0x03},
	{0x30,0x01},
	{0x33,0x01},
	{0x31,0x0a},
	{0x32,0x09},
	{0x38,0x01},
	{0x39,0x01},
	{0x3a,0x01},
	{0x3b,0x01},
	{0x4f,0x04},
	{0x4e,0x05},
	{0x50,0x01},
	{0x35,0x0c},
	{0x45,0x2a},
	{0x46,0x2a},
	{0x47,0x2a},
	{0x48,0x2a},
	{0x4a,0x2c},
	{0x4b,0x2c},
	{0x4c,0x2c},
	{0x4d,0x2c},
	{0x56,0x3a},
	{0x57,0x0a},
	{0x58,0x24},
	{0x59,0x20},
	{0x5a,0x0a},
	{0x5b,0xff},
	{0x37,0x0a},
	{0x42,0x0e},
	{0x68,0x90},
	{0x69,0xcd},
	{0x6a,0x8f},
	{0x7c,0x0a},
	{0x7d,0x09},
	{0x7e,0x09},
	{0x7f,0x08},
	{0x83,0x14},
	{0x84,0x14},
	{0x86,0x14},
	{0x87,0x07},
	{0x88,0x0f},
	{0x94,0x02},
	{0x98,0xd1},
	{0xfe,0x02},
	{0xfd,0x03},
	{0x97,0x78},
	{0x98,0x78},
	{0x99,0x78},
	{0x9a,0x78},
	{0xa1,0x40},
	{0xb1,0x30},
	{0xae,0x0d},
	{0x88,0x5b},
	{0x89,0x7c},
	{0xb4,0x05},
	{0x8c,0x40},
	{0x8e,0x40},
	{0x90,0x40},
	{0x92,0x40},
	{0x9b,0x46},
	{0xac,0x40},
	{0xfd,0x00},
	{0x5a,0x15},
	{0x74,0x01},
	{0xfd,0x00},
	{0x50,0x40},
	{0x52,0xb0},
	{0xfd,0x01},
	{0x03,0x70},
	{0x05,0x10},
	{0x07,0x20},
	{0x09,0xb0},
	{0x14,0x03},
	{0x15,0xF7},
	{0xfb,0x01},
	{0xfd,0x03},
	{0xc2,0x01},
	{0xfd,0x01},
#endif
	{SENSOR_REG_END, 0x00},
};

/*
 * the order of the sensor_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	/* 5M @25fps*/
	{
		.width = 1600,
		.height = 1200,
		.fps = 15 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SGRBG10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_1600_1200_15fps,
	},
};

struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[0];

/*
 * the part of driver was fixed.
 */

static struct regval_list sensor_stream_on[] = {
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_stream_off[] = {
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
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
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
			if (vals->reg_num == SENSOR_REG_PAGE) {
				val &= 0xf8;
				val |= (vals->value & 0x07);
				ret = sensor_write(sd, vals->reg_num, val);
				ret = sensor_read(sd, vals->reg_num, &val);
			}
		}
		pr_debug("vals->reg_num:0x%02x, vals->value:0x%02x\n",vals->reg_num, val);
		vals++;
	}
	return 0;
}
#endif

static int sensor_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sensor_write(sd, vals->reg_num, vals->value);
			printk("write:{0x%4x,0x%2x}\n",vals->reg_num, vals->value);
			ret = sensor_read(sd, vals->reg_num, &val);
			printk("write:{0x%4x,0x%2x}\n",vals->reg_num, val);
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
	unsigned char v;
	int ret;

	ret = sensor_read(sd, 0x02, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sensor_read(sd, 0x03, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 16) | v;
	return 0;
}

static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = sensor_write(sd, 0xfd, 0x01);
	ret += sensor_write(sd, 0x0f, (unsigned char)(value & 0xff));
	ret += sensor_write(sd, 0x0e, (unsigned char)((value >> 8) & 0xff));
	ret += sensor_write(sd, 0xfe, 0x02);
	if (ret < 0)
		return ret;

	return 0;
}


static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = sensor_write(sd, 0xfd, 0x01);
	ret += sensor_write(sd, 0x22, value);
	ret += sensor_write(sd, 0xfe, 0x02);
	if (ret < 0)
		return ret;

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
	wsize = &sensor_win_sizes[0];
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
	int ret = 0;
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned int vb = 0;
	unsigned int vb_init = 0;
	unsigned int vts_init = 0;
	unsigned char val = 0;
	unsigned int newformat = 0; //the format is 24.8
	unsigned int max_fps = 0;

	sclk = SENSOR_SUPPORT_SCLK_FPS_15;
	max_fps = SENSOR_OUTPUT_MAX_FPS;
	vb_init = 1244;
	vts_init = 2454;

	//printk("\n fpsnum = 0x%x\n",fps);

	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_WARNING("warn: fps(%d) not in range\n", fps);
		return -1;
	}
	/* get hts */
	ret = sensor_write(sd, 0xfd, 0x01);
	ret += sensor_read(sd, 0x25, &val);
	hts = val<<8;
	ret += sensor_read(sd, 0x26, &val);
	hts |= val;

	/* get vb old */
	//ret += sensor_read(sd, 0x14, &val);
	//vb = val<<8;
	//ret += sensor_read(sd, 0x15, &val);
	//vb |= val;

	/* get vts old */
	ret += sensor_read(sd, 0x27, &val);
	vts = val<<8;
	ret += sensor_read(sd, 0x28, &val);
	vts |= val;

	if (0 != ret) {
		ISP_ERROR("Error: %s read error\n", SENSOR_NAME);
		return ret;
	}

	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	vb = vts - vts_init + vb_init;

	ret += sensor_write(sd, 0xfd, 0x01);
	ret += sensor_write(sd, 0x14, (vb >> 8) & 0xff);
	ret += sensor_write(sd, 0x15, vb & 0xff);
	ret += sensor_write(sd, 0xFE, 0x02);
	if (0 != ret) {
		ISP_ERROR("err: %s sensor_write err\n",__func__);
		return ret;
	}
	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 7;
	sensor->video.attr->integration_time_limit = vts - 7;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 7;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int sensor_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	unsigned char val = 0;

	ret = sensor_write(sd, 0xfd, 0x01);
	ret += sensor_read(sd, 0x12, &val);
	switch(enable)
	{
		case 0:
			val &= 0xFC;
		break;
		case 1:
			val = ((val&0xFD)|0x01);
		break;
		case 2:
			val = ((val&0xFE)|0x02);
		break;
		case 3:
			val |= 0x03;
		break;
	}
	ret += sensor_write(sd, 0x12, val);
	ret += sensor_write(sd, 0xfe, 0x02);

	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = NULL;
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

static int sensor_attr_check(struct tx_isp_subdev *sd)
{
    struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
    struct tx_isp_sensor_register_info *info = &sensor->info;
    unsigned long rate;

    wsize = &sensor_win_sizes[0];
    sensor_attr.max_integration_time_native = 2447;
    sensor_attr.integration_time_limit = 2447;
    sensor_attr.total_width = 814;
    sensor_attr.total_height = 1472;
    sensor_attr.max_integration_time = 2447;
    sensor_attr.one_line_expr_in_us = 19;
    sensor_attr.integration_time = 538;

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
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(20);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(20);
		} else {
			ISP_ERROR("gpio request fail %d\n",reset_gpio);
		}
	}
	if (pwdn_gpio != -1) {
		ret = private_gpio_request(pwdn_gpio,"sensor_pwdn");
		if (!ret) {
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(pwdn_gpio, 1);
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
	if (chip) {
		memcpy(chip->name, SENSOR_NAME, sizeof(SENSOR_NAME));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}
	return 0;
}

static int sensor_st(struct tx_isp_subdev *sd, int enable)
{
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
	//return 0;
	switch(cmd) {
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if (arg)
			ret = sensor_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if (arg)
			ret = sensor_set_analog_gain(sd, sensor_val->value);
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
		if (arg)
			ret = sensor_write_array(sd, sensor_stream_off);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		if (arg)
			ret = sensor_write_array(sd, sensor_stream_on);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if (arg)
			ret = sensor_set_fps(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if (arg)
			ret = sensor_set_vflip(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_LOGIC:
		if (arg)
			ret = sensor_st(sd, sensor_val->value);
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
	sensor_write(sd, reg->reg & 0xff, reg->val & 0xff);

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
	sensor->video.shvflip = shvflip;
	sensor->video.attr = &sensor_attr;
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
