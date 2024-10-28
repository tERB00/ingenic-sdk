#ifndef _TX_ISP_DEBUG_H_
#define _TX_ISP_DEBUG_H_

#include <linux/hrtimer.h>
#include <linux/dma-mapping.h>
#include <txx-funcs.h>

/* =================== switchs ================== */

/**
 * default debug level, if just switch ISP_WARNING
 * or ISP_INFO, this not effect DEBUG_REWRITE and
 * DEBUG_TIME_WRITE/READ
 **/
/* =================== print tools ================== */

#define ISP_INFO_LEVEL		0x0
#define ISP_WARNING_LEVEL	0x1
#define ISP_ERROR_LEVEL		0x2
#define ISP_PRINT(level, format, ...)			\
	isp_printf(level, format, ##__VA_ARGS__)
#define ISP_INFO(...) ISP_PRINT(ISP_INFO_LEVEL, __VA_ARGS__)
#define ISP_WARNING(...) ISP_PRINT(ISP_WARNING_LEVEL, __VA_ARGS__)
#define ISP_ERROR(...) ISP_PRINT(ISP_ERROR_LEVEL, __VA_ARGS__)

//extern unsigned int isp_print_level;
/*int isp_debug_init(void);*/
/*int isp_debug_deinit(void);*/
extern char *sclk_name[2];
int isp_printf(unsigned int level, unsigned char *fmt, ...);
int get_isp_clk(void);
int get_isp_clka(void);
#ifdef SENSOR_DOUBLE
int get_mipi_switch_gpio(void);
#endif
char *get_clk_name(void);
char *get_clka_name(void);
#ifndef TX_ISP_MALLOC_TEST
void *private_vmalloc(unsigned long size);
void private_vfree(const void *addr);
#else
#include <linux/vmalloc.h>
extern void *vmalloc_t;
#define private_vmalloc(size) \
        (vmalloc_t = vmalloc(size)); \
        printk("[%s %d] vmalloc addr is %p, size is %d\n", __func__, __LINE__, vmalloc_t, size)
#define private_vfree(addr) \
        vfree(addr); \
        printk("[%s %d] vfree addr is %p\n", __func__, __LINE__, addr)
#endif /* TX_ISP_MALLOC_TEST */

ktime_t private_ktime_set(const long secs, const unsigned long nsecs);
void private_set_current_state(unsigned int state);
int private_schedule_hrtimeout(ktime_t *ex, const enum hrtimer_mode mode);
bool private_schedule_work(struct work_struct *work);
void private_do_gettimeofday(struct timeval *tv);
void private_dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir);
__must_check int private_get_driver_interface(struct jz_driver_common_interfaces **pfaces);
#ifdef SENSOR_DOUBLE
struct pwm_device *private_pwm_request(int pwm, const char *label);
void private_pwm_set_period(struct pwm_device *pwm, unsigned int period);
void private_pwm_set_duty_cycle(struct pwm_device *pwm, unsigned int duty);
int private_pwm_enable(struct pwm_device *pwm);
void private_pwm_disable(struct pwm_device *pwm);
void private_pwm_free(struct pwm_device *pwm);
#endif
#endif /* _ISP_DEBUG_H_ */
