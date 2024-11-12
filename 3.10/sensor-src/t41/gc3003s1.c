// SPDX-License-Identifier: GPL-2.0+
/*
 * gc3003s1.c
 *
 * master sensor fsync to slaver sensor fsync
 *
 * Settings:
 * sboot        resolution       fps     interface              mode
 *   0          2304*1296        15     mipi_2lane             linear
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
#include <sensor-info.h>

#define SENSOR_NAME "gc3003s1"
#define SENSOR_CHIP_ID_H (0x30)
#define SENSOR_CHIP_ID_L (0x03)
#define SENSOR_REG_END 0xffff
#define SENSOR_REG_DELAY 0xfffe
#define SENSOR_OUTPUT_MAX_FPS 30
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION "H20231103a"

static int reset_gpio = -1;
static int pwdn_gpio = -1;
static int shvflip = 1;

static int fsync_mode = 3;
module_param(fsync_mode, int, S_IRUGO);
MODULE_PARM_DESC(fsync_mode, "Sensor Indicates the frame synchronization mode");

struct regval_list {
    uint16_t reg_num;
    unsigned char value;
};

struct again_lut {
	int index;
	unsigned int again_reg_val_0;
	unsigned int again_reg_val_1;
	unsigned int again_reg_val_2;
	unsigned int again_reg_val_3;
	unsigned int again_reg_val_4;
	unsigned int again_reg_val_5;
	unsigned int gain;
};

struct again_lut sensor_again_lut[] = {
/*index    00d1  00d0  0080  0155  00b8  00b9 ispgain */
	{0x00, 0x00, 0x00, 0x05, 0x01, 0x01, 0x00, 0},   // 1.000000
	{0x01, 0x0a, 0x00, 0x06, 0x01, 0x01, 0x0c, 16247},   // 1.187500
	{0x02, 0x00, 0x01, 0x06, 0x01, 0x01, 0x1a, 32233},   // 1.406250
	{0x03, 0x0a, 0x01, 0x08, 0x01, 0x01, 0x2a, 47704},   // 1.656250
	{0x04, 0x20, 0x00, 0x0a, 0x02, 0x02, 0x00, 65536},   // 2.000000
	{0x05, 0x25, 0x00, 0x0a, 0x03, 0x02, 0x18, 81783},   // 2.375000
	{0x06, 0x20, 0x01, 0x0a, 0x04, 0x02, 0x33, 97243},   // 2.796875
	{0x07, 0x25, 0x01, 0x0b, 0x05, 0x03, 0x14, 113240},   // 3.312500
	{0x08, 0x30, 0x00, 0x0b, 0x06, 0x04, 0x00, 131072},   // 4.000000
	{0x09, 0x32, 0x80, 0x0c, 0x09, 0x04, 0x2f, 147007},   // 4.734375
	{0x0a, 0x30, 0x01, 0x0c, 0x0c, 0x05, 0x26, 162779},   // 5.593750
	{0x0b, 0x32, 0x81, 0x0d, 0x0e, 0x06, 0x29, 178999},   // 6.640625
	{0x0c, 0x38, 0x00, 0x0e, 0x10, 0x08, 0x00, 196608},   // 8.000000
	{0x0d, 0x39, 0x40, 0x10, 0x12, 0x09, 0x1f, 212700},   // 9.484375
	{0x0e, 0x38, 0x01, 0x12, 0x12, 0x0b, 0x0d, 228315},   // 11.187500
	{0x0f, 0x39, 0x41, 0x14, 0x14, 0x0d, 0x12, 244312},   // 13.250000
	{0x10, 0x30, 0x08, 0x15, 0x16, 0x10, 0x00, 262144},   // 16.000000
	{0x11, 0x32, 0x88, 0x18, 0x1a, 0x12, 0x3e, 278236},   // 18.968750
	{0x12, 0x30, 0x09, 0x1a, 0x1d, 0x16, 0x1a, 293982},   // 22.406250
	{0x13, 0x32, 0x89, 0x1c, 0x22, 0x1a, 0x23, 310015},   // 26.546875
	{0x14, 0x38, 0x08, 0x1e, 0x26, 0x20, 0x00, 327680},   // 32.000000
	{0x15, 0x39, 0x48, 0x20, 0x2d, 0x25, 0x3b, 343732},   // 37.921875
	{0x16, 0x38, 0x09, 0x22, 0x32, 0x2c, 0x33, 359419},   // 44.765625
	{0x17, 0x39, 0x49, 0x24, 0x3a, 0x35, 0x06, 375411},   // 53.015625
	{0x18, 0x38, 0x0a, 0x26, 0x42, 0x3f, 0x3f, 393216},   // 64.000000
};

struct tx_isp_sensor_attribute sensor_attr;

unsigned int sensor_alloc_integration_time(unsigned int it, unsigned char shift, unsigned int *sensor_it) {
	unsigned int expo = it >> shift;
	unsigned int isp_it = it;

	*sensor_it = expo;

	return isp_it;
}

unsigned int sensor_alloc_integration_time_short(unsigned int it, unsigned char shift, unsigned int *sensor_it) {
	unsigned int expo = it >> shift;
	unsigned int isp_it = it;
	*sensor_it = expo;
	return isp_it;
}

unsigned int sensor_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again) {
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again) {
		if (isp_gain == 0) {
			*sensor_again = 0;
			return lut[0].gain;
		} else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->index;
			return (lut - 1)->gain;
		} else {
			if ((lut->gain == sensor_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->index;
				return lut->gain;
			}
		}

		lut++;
	}

	return 0;
}

unsigned int sensor_alloc_again_short(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again) {
	struct again_lut *lut = sensor_again_lut;
	while (lut->gain <= sensor_attr.max_again) {
		if (isp_gain == 0) {
			*sensor_again = 0;
			return lut[0].gain;
		} else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->index;
			return (lut - 1)->gain;
		} else {
			if ((lut->gain == sensor_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->index;
				return lut->gain;
			}
		}

		lut++;
	}

	return 0;
}

unsigned int sensor_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain) {
	return 0;
}

struct tx_isp_mipi_bus sensor_mipi_linear = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 747,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.image_twidth = 2304,
	.image_theight = 1296,
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
	.mipi_sc.data_type_value = 0,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};

struct tx_isp_sensor_attribute sensor_attr = {
	.name = SENSOR_NAME,
	.chip_id = 0x3003,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_16BITS,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.data_type = TX_SENSOR_DATA_TYPE_LINEAR,
	.cbus_device = 0x37,
	.max_again = 393216,
	.max_dgain = 0,
	.expo_fs = 1,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = sensor_alloc_again,
	.sensor_ctrl.alloc_again_short = sensor_alloc_again_short,
	.sensor_ctrl.alloc_dgain = sensor_alloc_dgain,
	.sensor_ctrl.alloc_integration_time_short = sensor_alloc_integration_time_short,
	.fsync_attr = {
		.mode = TX_ISP_SENSOR_FSYNC_MODE_MS_REALTIME_MISPLACE,
		.call_times = 1,
		.sdelay = 1000,
	}
};

static struct regval_list sensor_init_regs_2304_1296_30fps_mipi[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03f3, 0x00},
	{0x03f5, 0xc0},
	{0x03f6, 0x06},
	{0x03f7, 0x01},
	{0x03f8, 0x53},
	{0x03f9, 0x13},
	{0x03fa, 0x00},
	{0x03e0, 0x16},
	{0x03e1, 0x0d},
	{0x03e2, 0x40},
	{0x03e4, 0x08},
	{0x03fc, 0xce},
	{0x0d05, 0x05},
	{0x0d06, 0x6d},
	{0x0d41, 0x05},
	{0x0d42, 0x3c},
	{0x0d0a, 0x02},
	{0x000c, 0x02},
	{0x0d0d, 0x05},
	{0x0d0e, 0x18},
	{0x000f, 0x09},
	{0x0010, 0x08},
	{0x0017, 0x0c},
	{0x0d53, 0x12},
	{0x0051, 0x03},
	{0x0082, 0x01},
	{0x0086, 0x20},
	{0x008a, 0x01},
	{0x008b, 0x1d},
	{0x008c, 0x05},
	{0x008d, 0xd0},
	{0x0db7, 0x01},
	{0x0db0, 0x05},
	{0x0db1, 0x00},
	{0x0db2, 0x04},
	{0x0db3, 0x54},
	{0x0db4, 0x00},
	{0x0db5, 0x17},
	{0x0db6, 0x08},
	{0x0d25, 0xcb},
	{0x0d4a, 0x04},
	{0x00d2, 0x70},
	{0x00d7, 0x19},
	{0x00d9, 0x10},
	{0x00da, 0xc1},
	{0x0d55, 0x1b},
	{0x0d92, 0x17},
	{0x0dc2, 0x30},
	{0x0d2a, 0x30},
	{0x0d19, 0x51},
	{0x0d29, 0x40},
	{0x0d20, 0x40},
	{0x0d72, 0x12},
	{0x0d4e, 0x12},
	{0x0d43, 0x2b},
	{0x0050, 0x0c},
	{0x006e, 0x03},
	{0x0153, 0x50},
	{0x0192, 0x04},
	{0x0194, 0x04},
	{0x0195, 0x05},
	{0x0196, 0x10},
	{0x0197, 0x09},
	{0x0198, 0x00},
	{0x0077, 0x01},
	{0x0078, 0x65},
	{0x0079, 0x04},
	{0x0067, 0xc0},
	{0x0054, 0xff},
	{0x0055, 0x02},
	{0x0056, 0x00},
	{0x0057, 0x04},
	{0x005a, 0xff},
	{0x005b, 0x07},
	{0x00d5, 0x03},
	{0x0102, 0x10},
	{0x0d4a, 0x04},
	{0x04e0, 0xff},
	{0x031e, 0x3e},
	{0x0159, 0x01},
	{0x014f, 0x28},
	{0x0150, 0x40},
	{0x0414, 0x75},
	{0x0415, 0x75},
	{0x0416, 0x75},
	{0x0417, 0x75},
	{0x0155, 0x00},
	{0x0428, 0x0b},
	{0x0429, 0x0b},
	{0x042a, 0x0b},
	{0x042b, 0x0b},
	{0x042c, 0x0b},
	{0x042d, 0x0b},
	{0x042e, 0x0b},
	{0x042f, 0x0b},
	{0x0430, 0x05},
	{0x0431, 0x05},
	{0x0432, 0x05},
	{0x0433, 0x05},
	{0x0434, 0x04},
	{0x0435, 0x04},
	{0x0436, 0x04},
	{0x0437, 0x04},
	{0x0438, 0x18},
	{0x0439, 0x18},
	{0x043a, 0x18},
	{0x043b, 0x18},
	{0x043c, 0x1d},
	{0x043d, 0x20},
	{0x043e, 0x22},
	{0x043f, 0x24},
	{0x0468, 0x04},
	{0x0469, 0x04},
	{0x046a, 0x04},
	{0x046b, 0x04},
	{0x046c, 0x04},
	{0x046d, 0x04},
	{0x046e, 0x04},
	{0x046f, 0x04},
	{0x0108, 0xf0},
	{0x0109, 0x80},
	{0x0d03, 0x05},
	{0x0d04, 0x00},
	{0x007a, 0x60},
	{0x00d0, 0x00},
	{0x0080, 0x09},
	{0x0291, 0x0f},
	{0x0292, 0xff},
	{0x0201, 0x27},
	{0x0202, 0x53},
	{0x0203, 0x4e},
	{0x0206, 0x03},
	{0x0212, 0x0b},
	{0x0213, 0x40},
	{0x0215, 0x12},
	{0x023e, 0x99},
	{0x03fe, 0x10},
	{0x0183, 0x09},
	{0x0187, 0x51},
	{0x0d22, 0x04},
	{0x0d21, 0x3C},
	{0x0d03, 0x01},
	{0x0d04, 0x28},
	{0x0d23, 0x0e},
	{0x03fe, 0x00},
	{0x0d76, 0x00},
	{0x0d41, 0x06},
	{0x0d42, 0xc0},
	{0x00d7, 0x1b},
	{0x008c, 0x03},
	{0x008d, 0xe8},
	{0x007a, 0x98},
	{SENSOR_REG_END, 0x00},
};

/*
 * the order of the jxf23_win_sizes is [full_resolution, preview_resolution]. */
static struct tx_isp_sensor_win_setting sensor_win_sizes[] = {
	/* [0] 2880*1620 @ max 30fps*/
	{
		.width = 2304,
		.height = 1296,
		.fps = 30 << 16 | 1,
		.mbus_code = TISP_VI_FMT_SRGGB10_1X10,
		.colorspace = TISP_COLORSPACE_SRGB,
		.regs = sensor_init_regs_2304_1296_30fps_mipi,
	},
};

struct tx_isp_sensor_win_setting *wsize = &sensor_win_sizes[0];

static struct regval_list sensor_stream_on[] = {
	{SENSOR_REG_END, 0x00},
};

static struct regval_list sensor_stream_off[] = {
	{SENSOR_REG_END, 0x00},
};

int sensor_read(struct tx_isp_subdev *sd, uint16_t reg,
		unsigned char *value) {
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
		 unsigned char value) {
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
			private_msleep(vals->value);
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

static int sensor_write_array(struct tx_isp_subdev *sd, struct regval_list *vals) {
	int ret;
	while (vals->reg_num != SENSOR_REG_END) {
		if (vals->reg_num == SENSOR_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sensor_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int sensor_reset(struct tx_isp_subdev *sd, struct tx_isp_initarg *init) {
	return 0;
}

static int sensor_detect(struct tx_isp_subdev *sd, unsigned int *ident) {
	unsigned char v = 0;
	int ret;

	ret = sensor_read(sd, 0x03f0, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret, v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_H)
		return -ENODEV;
	ret = sensor_read(sd, 0x03f1, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret, v);
	if (ret < 0)
		return ret;
	if (v != SENSOR_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int sensor_set_expo(struct tx_isp_subdev *sd, int value) {
	int ret = 0;
	int it = (value & 0xffff);
	int again = (value & 0xffff0000) >> 16;
	struct again_lut *val_lut = sensor_again_lut;

	/*set analog gain*/
	ret += sensor_write(sd, 0x00d1, val_lut[again].again_reg_val_0);
	ret += sensor_write(sd, 0x00d0, val_lut[again].again_reg_val_1);
	ret += sensor_write(sd, 0x0080, val_lut[again].again_reg_val_2);
	ret += sensor_write(sd, 0x0155, val_lut[again].again_reg_val_3);
	ret += sensor_write(sd, 0x00b8, val_lut[again].again_reg_val_4);
	ret += sensor_write(sd, 0x00b9, val_lut[again].again_reg_val_5);
	/*integration time*/
	ret += sensor_write(sd, 0x0d03, (unsigned char) ((it >> 8) & 0xff));
	ret += sensor_write(sd, 0x0d04, (unsigned char) (it & 0xff));
	if (ret < 0)
		ISP_ERROR("sensor_write error  %d\n", __LINE__);

	return ret;
}

# if  0
static int sensor_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sensor_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}
#endif

static int sensor_set_digital_gain(struct tx_isp_subdev *sd, int value) {
	return 0;
}

static int sensor_get_black_pedestal(struct tx_isp_subdev *sd, int value) {
	return 0;
}

static int sensor_set_attr(struct tx_isp_subdev *sd, struct tx_isp_sensor_win_setting *wise) {
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.max_fps = wsize->fps;
	sensor->video.min_fps = SENSOR_OUTPUT_MIN_FPS << 16 | 1;

	return 0;
}

static int sensor_init(struct tx_isp_subdev *sd, struct tx_isp_initarg *init) {
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

static int sensor_s_stream(struct tx_isp_subdev *sd, struct tx_isp_initarg *init) {
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
			ISP_WARNING("%s stream on\n", SENSOR_NAME);
			sensor->video.state = TX_ISP_MODULE_RUNNING;
		}
	} else {
		ret = sensor_write_array(sd, sensor_stream_off);
		pr_debug("%s stream off\n", SENSOR_NAME);
	}

	return ret;
}

static int sensor_set_fps(struct tx_isp_subdev *sd, int fps) {
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int wpclk = 0;
	unsigned short vts = 0;
	unsigned short hts = 0;
	unsigned char tmp;
	unsigned int newformat = 0; //the format is 24.8
	unsigned char sensor_max_fps;

	int ret = 0;

	switch (sensor->info.default_boot) {
		case 0:
			wpclk = 1389 * 1728 * 30;
			sensor_max_fps = TX_SENSOR_MAX_FPS_30;
			break;
		default:
			ISP_ERROR("Now we do not support this framerate!!!\n");
	}

	/* the format of fps is 16/16. for example 30 << 16 | 2, the value is 30/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if (newformat > (sensor_max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%x) not in range\n", fps);
		return -1;
	}

	/*get current hts*/
	ret += sensor_read(sd, 0xd05, &tmp);
	hts = tmp;
	ret += sensor_read(sd, 0xd06, &tmp);
	if (ret < 0)
		return -1;
	hts = ((hts << 8) + tmp);

	/*calculate and set vts for given fps*/
	vts = wpclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	ret = sensor_write(sd, 0x0d41, (unsigned char) ((vts & 0xff00) >> 8));
	ret += sensor_write(sd, 0x0d42, (unsigned char) (vts & 0xff));
	if (ret < 0)
		return ret;

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 8;
	sensor->video.attr->integration_time_limit = vts - 8;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 8;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return 0;
}

static int sensor_set_hvflip(struct tx_isp_subdev *sd, int enable) {
	int ret = 0;
	uint8_t val, val1;

	ret += sensor_read(sd, 0x0015, &val);
	ret += sensor_read(sd, 0x0d15, &val1);

	switch (enable) {
		case 0:
			val &= 0xfC;
			val1 &= 0xfC;
			break;
		case 1:
			val = ((val & 0xfd) | 0x01);
			val1 = ((val1 & 0xfd) | 0x01);
			break;
		case 2:
			val = ((val & 0xfe) | 0x02);
			val1 = ((val1 & 0xfe) | 0x02);
			break;
		case 3:
			val |= 0x03;
			val1 |= 0x03;
			break;
	}

	ret += sensor_write(sd, 0x0015, val);
	ret += sensor_write(sd, 0x0d15, val1);

	return ret;
}

static int sensor_set_mode(struct tx_isp_subdev *sd, int value) {
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if (wsize) {
		sensor_set_attr(sd, wsize);
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}
	return ret;
}

struct clk *sclka;

static int sensor_attr_check(struct tx_isp_subdev *sd) {
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	struct clk *sclka;
	unsigned long rate;
	int ret = 0;

	switch (info->default_boot) {
		case 0:
			wsize = &sensor_win_sizes[0];
			sensor_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
			memcpy(&sensor_attr.mipi, &sensor_mipi_linear, sizeof(sensor_mipi_linear));
			sensor_attr.one_line_expr_in_us = 19;
			sensor_attr.total_width = 1389;
			sensor_attr.total_height = 1728;
			sensor_attr.max_integration_time_native = 1728 - 8;
			sensor_attr.integration_time_limit = 1728 - 8;
			sensor_attr.max_integration_time = 1728 - 8;
			sensor_attr.again = 0;
			sensor_attr.integration_time = 0x128;
			break;
		default:
			ISP_ERROR("Have no this setting!!!\n");
	}
	sensor_attr.fsync_attr.mode = fsync_mode;
	if (fsync_mode == TX_ISP_SENSOR_FSYNC_MODE_MS_REALTIME_MISPLACE) {
		sensor_attr.total_height = ((sensor_attr.total_height) << 1);
		wsize->fps = ((wsize->fps & 0xffff0000) | ((wsize->fps & 0xffff) << 1));
	}
	sensor_attr.max_integration_time_native = sensor_attr.total_height - 8;
	sensor_attr.integration_time_limit = sensor_attr.total_height - 8;
	sensor_attr.max_integration_time = sensor_attr.total_height - 8;

	switch (info->mclk) {
		case TISP_SENSOR_MCLK0:
		case TISP_SENSOR_MCLK1:
		case TISP_SENSOR_MCLK2:
			sclka = private_devm_clk_get(&client->dev, SEN_MCLK);
			sensor->mclk = private_devm_clk_get(sensor->dev, SEN_BCLK);
			set_sensor_mclk_function(0);
			break;
		default:
			ISP_ERROR("Have no this MCLK Source!!!\n");
	}

	rate = private_clk_get_rate(sensor->mclk);
	switch (info->default_boot) {
		case 0:
			if (((rate / 1000) % 27000) != 0) {
				ret = clk_set_parent(sclka, clk_get(NULL, SEN_TCLK));
				sclka = private_devm_clk_get(&client->dev, SEN_TCLK);
				if (IS_ERR(sclka)) {
					pr_err("get sclka failed\n");
				} else {
					rate = private_clk_get_rate(sclka);
					if (((rate / 1000) % 27000) != 0) {
						private_clk_set_rate(sclka, 1188000000);
					}
				}
			}
			private_clk_set_rate(sensor->mclk, 27000000);
			private_clk_prepare_enable(sensor->mclk);
			break;
	}

	ISP_WARNING("\n====>[default_boot=%d] [resolution=%dx%d] [video_interface=%d] [MCLK=%d] \n", info->default_boot,
		    wsize->width, wsize->height, info->video_interface, info->mclk);
	reset_gpio = info->rst_gpio;
	pwdn_gpio = info->pwdn_gpio;

	sensor_set_attr(sd, wsize);
	sensor->priv = wsize;
	return 0;
}

static int sensor_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip) {
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	sensor_attr_check(sd);
#if 0
	if (reset_gpio != -1) {
		ret = private_gpio_request(reset_gpio,"sensor_reset");
		if (!ret) {
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(20);
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
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(10);
		} else {
			ISP_ERROR("gpio request fail %d\n",pwdn_gpio);
		}
	}
#endif
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

static int sensor_fsync(struct tx_isp_subdev *sd, struct tx_isp_sensor_fsync *fsync) {
	uint8_t val;
	uint16_t ret_val;

	printk("=========>> [%s %d]\n", __func__, __LINE__);
	if (fsync->place != TX_ISP_SENSOR_FSYNC_PLACE_STREAMON_AFTER)
		return 0;
	switch (fsync->call_index) {
		case 0:
			switch (fsync_mode) {
				case 2:
					printk("[%s %d] -> mode 2\n", __func__, __LINE__);
					sensor_write(sd, 0x0068, 0x93);
					sensor_write(sd, 0x0069, 0x00);
					sensor_write(sd, 0x0d67, 0x01);
					sensor_write(sd, 0x0d69, 0x00);
					sensor_write(sd, 0x0d6a, 0x08);
					sensor_write(sd, 0x0d6b, 0x50);
					sensor_write(sd, 0x0d6c, 0x00);
					sensor_write(sd, 0x0d6e, 0x00);
					sensor_write(sd, 0x0d6f, 0x04);
					sensor_write(sd, 0x0d70, 0x00);
					sensor_write(sd, 0x0d71, 0x12);
					break;
				case 3:
					printk("[%s %d] -> mode 3\n", __func__, __LINE__);
					sensor_read(sd, 0x0d41, &val);
					ret_val = val << 8;
					sensor_read(sd, 0x0d42, &val);
					ret_val = (ret_val | val) << 1;
					sensor_write(sd, 0x0d41, ((ret_val >> 8) & 0xff));
					sensor_write(sd, 0x0d42, (ret_val & 0xff));
					sensor_write(sd, 0x0068, 0x93);
					sensor_write(sd, 0x0069, 0x00);
					sensor_write(sd, 0x0d67, 0x00);
					sensor_write(sd, 0x0d69, 0x7f);
					sensor_write(sd, 0x0d6a, 0x04);
					sensor_write(sd, 0x0d6c, 0x02);
					sensor_write(sd, 0x0d6d, 0x89);
					sensor_write(sd, 0x0d6e, 0x00);
					sensor_write(sd, 0x0d6f, 0x04);
					sensor_write(sd, 0x0d70, 0x00);
					sensor_write(sd, 0x0d71, 0x30);
					sensor_write(sd, 0x0d73, 0x92);
					sensor_write(sd, 0x0d6b, 0x51);
					break;
			}
			break;
	}

	return 0;
}

static int sensor_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg) {
	long ret = 0;
	struct tx_isp_sensor_value *sensor_val = arg;

	if (IS_ERR_OR_NULL(sd)) {
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch (cmd) {
		case TX_ISP_EVENT_SENSOR_EXPO:
			if (arg)
				ret = sensor_set_expo(sd, sensor_val->value);
			break;
		case TX_ISP_EVENT_SENSOR_INT_TIME:
			//if (arg)
			//	ret = sensor_set_integration_time(sd, sensor_val->value);
			break;
		case TX_ISP_EVENT_SENSOR_AGAIN:
			//if (arg)
			//	ret = sensor_set_analog_gain(sd, sensor_val->value);
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

	return ret;
}

static int sensor_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg) {
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

static int sensor_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg) {
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
	.fsync = sensor_fsync,
};

static struct tx_isp_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.sensor = &sensor_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64) 0;
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

static int sensor_probe(struct i2c_client *client, const struct i2c_device_id *id) {
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;

	sensor = (struct tx_isp_sensor *) kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0, sizeof(*sensor));
	sensor->dev = &client->dev;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.shvflip = shvflip;
	sensor_attr.expo_fs = 1;
	sensor->video.attr = &sensor_attr;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sensor_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->%s\n", SENSOR_NAME);

	return 0;
}

static int sensor_remove(struct i2c_client *client) {
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
	{SENSOR_NAME, 0},
	{}
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

static __init int init_sensor(void) {
	return private_i2c_add_driver(&sensor_driver);
}

static __exit void exit_sensor(void) {
	private_i2c_del_driver(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

MODULE_DESCRIPTION("A low-level driver for "SENSOR_NAME" sensor");
MODULE_LICENSE("GPL");
