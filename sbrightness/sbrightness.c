#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DEFAULT_STEP	5
#define DDC_ADDR	0x37
#define VCP_BRIGHTNESS	0x10

/* sysfs backlight */

static int
bl_read_int(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	int v = -1;
	fscanf(f, "%d", &v);
	fclose(f);
	return v;
}

static int
bl_write_int(const char *path, int v)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "%d\n", v);
	fclose(f);
	return 0;
}

/*
 * Find the first backlight device under /sys/class/backlight that is
 * associated with the given DRM connector name. This is done by checking
 * symlinks under /sys/class/drm/<connector>/device/backlight/.
 */
static int
bl_find(const char *connector, char *out, size_t outsz)
{
	char path[512];
	snprintf(path, sizeof(path),
		"/sys/class/drm/card0-%s/device/backlight", connector);

	DIR *d = opendir(path);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			snprintf(out, outsz, "/sys/class/backlight/%s", e->d_name);
			closedir(d);
			return 0;
		}
		closedir(d);
	}

	/* fallback: first entry in /sys/class/backlight */
	d = opendir("/sys/class/backlight");
	if (!d) return -1;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		snprintf(out, outsz, "/sys/class/backlight/%s", e->d_name);
		closedir(d);
		return 0;
	}
	closedir(d);
	return -1;
}

static int
bl_adjust(const char *connector, int delta)
{
	char base[512];
	if (bl_find(connector, base, sizeof(base)) < 0) {
		fprintf(stderr, "sbrightness: no backlight found for %s\n", connector);
		return -1;
	}

	char path[560];
	snprintf(path, sizeof(path), "%s/max_brightness", base);
	int max = bl_read_int(path);
	if (max <= 0) max = 100;

	snprintf(path, sizeof(path), "%s/brightness", base);
	int cur = bl_read_int(path);
	if (cur < 0) cur = max;

	int step = (max * abs(delta)) / 100;
	if (step < 1) step = 1;

	int next = cur + (delta > 0 ? step : -step);
	if (next < 0)    next = 0;
	if (next > max)  next = max;

	if (bl_write_int(path, next) < 0) {
		fprintf(stderr, "sbrightness: cannot write %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	return 0;
}

/* DDC/CI over I2C (external monitors) */

/*
 * Locate the I2C bus for a given DRM connector by searching all cardN entries
 * under /sys/class/drm/ for cardN-<connector>/ddc or cardN-<connector>/i2c.
 */
static int
ddc_find_bus(const char *connector)
{
	DIR *d = opendir("/sys/class/drm");
	if (!d) return -1;

	int bus = -1;
	struct dirent *e;
	while ((e = readdir(d)) && bus < 0) {
		/* match entries of the form cardN-<connector> */
		if (strncmp(e->d_name, "card", 4) != 0) continue;
		const char *dash = strchr(e->d_name + 4, '-');
		if (!dash) continue;
		if (strcmp(dash + 1, connector) != 0) continue;

		char path[512];
		char target[256] = {0};
		ssize_t n;

		/* try ddc symlink first, then i2c */
		snprintf(path, sizeof(path), "/sys/class/drm/%s/ddc", e->d_name);
		n = readlink(path, target, sizeof(target) - 1);
		if (n < 0) {
			snprintf(path, sizeof(path), "/sys/class/drm/%s/i2c", e->d_name);
			n = readlink(path, target, sizeof(target) - 1);
		}
		if (n < 0) continue;
		target[n] = '\0';

		/* target is something like ../../i2c-5, just i2c-5 or whatever */
		char *p = strrchr(target, '/');
		p = p ? p + 1 : target;
		if (strncmp(p, "i2c-", 4) != 0) continue;
		bus = atoi(p + 4);
	}

	closedir(d);
	return bus;
}

/*
 * DDC/CI get VCP feature value.
 * Returns current value 0-100 or -1 on failure.
 */
static int
ddc_getvcp(int fd, uint8_t vcp)
{
	/* send Get VCP Feature request */
	uint8_t req[5];
	req[0] = 0x51; /* source: host */
	req[1] = 0x82; /* length: 2 bytes follow */
	req[2] = 0x01; /* Get VCP Feature opcode */
	req[3] = vcp;
	uint8_t cksum = 0x6e ^ req[0] ^ req[1] ^ req[2] ^ req[3];
	req[4] = cksum;

	struct i2c_msg msgs[1] = {{
		.addr  = DDC_ADDR,
		.flags = 0,
		.len   = sizeof(req),
		.buf   = req,
	}};
	struct i2c_rdwr_ioctl_data data = { .msgs = msgs, .nmsgs = 1 };
	if (ioctl(fd, I2C_RDWR, &data) < 0) return -1;

	usleep(50000); /* DDC/CI mandates 50ms before reply */

	/* read Get VCP Feature reply (12 bytes) */
	uint8_t rep[12] = {0};
	struct i2c_msg rmsgs[1] = {{
		.addr  = DDC_ADDR,
		.flags = I2C_M_RD,
		.len   = sizeof(rep),
		.buf   = rep,
	}};
	struct i2c_rdwr_ioctl_data rdata = { .msgs = rmsgs, .nmsgs = 1 };
	if (ioctl(fd, I2C_RDWR, &rdata) < 0) return -1;

	/* rep[6..7] = max value, rep[8..9] = current value (big-endian) */
	int max = (rep[6] << 8) | rep[7];
	int cur = (rep[8] << 8) | rep[9];
	if (max <= 0) return -1;
	return (cur * 100) / max;
}

/*
 * DDC/CI set VCP feature to an absolute value (0-100 scaled to monitor max).
 */
static int
ddc_setvcp(int fd, uint8_t vcp, int val_percent, int max_val)
{
	int raw = (val_percent * max_val) / 100;
	if (raw < 0)        raw = 0;
	if (raw > max_val)  raw = max_val;

	uint8_t hi = (raw >> 8) & 0xff;
	uint8_t lo = raw & 0xff;

	uint8_t req[7];
	req[0] = 0x51;
	req[1] = 0x84; /* length: 0x80 | 4 (opcode + vcp + hi + lo) */
	req[2] = 0x03; /* Set VCP Feature opcode */
	req[3] = vcp;
	req[4] = hi;
	req[5] = lo;
	uint8_t cksum = 0x6e ^ req[0] ^ req[1] ^ req[2] ^ req[3] ^ req[4] ^ req[5];
	req[6] = cksum;

	struct i2c_msg msgs[1] = {{
		.addr  = DDC_ADDR,
		.flags = 0,
		.len   = sizeof(req),
		.buf   = req,
	}};
	struct i2c_rdwr_ioctl_data data = { .msgs = msgs, .nmsgs = 1 };
	fprintf(stderr, "ddc_setvcp: vcp=0x%02x raw=%d (from %d%% of max %d)\n",
		vcp, raw, val_percent, max_val);
	if (ioctl(fd, I2C_RDWR, &data) < 0) return -1;
	usleep(50000); /* DDC/CI mandates 50ms after Set VCP */
	return 0;
}

static int
ddc_get_max(int fd, uint8_t vcp)
{
	uint8_t req[5];
	req[0] = 0x51;
	req[1] = 0x82;
	req[2] = 0x01;
	req[3] = vcp;
	req[4] = 0x6e ^ req[0] ^ req[1] ^ req[2] ^ req[3];

	struct i2c_msg msgs[1] = {{
		.addr  = DDC_ADDR,
		.flags = 0,
		.len   = sizeof(req),
		.buf   = req,
	}};
	struct i2c_rdwr_ioctl_data data = { .msgs = msgs, .nmsgs = 1 };
	if (ioctl(fd, I2C_RDWR, &data) < 0) return 100;

	usleep(50000);

	uint8_t rep[12] = {0};
	struct i2c_msg rmsgs[1] = {{
		.addr  = DDC_ADDR,
		.flags = I2C_M_RD,
		.len   = sizeof(rep),
		.buf   = rep,
	}};
	struct i2c_rdwr_ioctl_data rdata = { .msgs = rmsgs, .nmsgs = 1 };
	if (ioctl(fd, I2C_RDWR, &rdata) < 0) return 100;

	int max = (rep[6] << 8) | rep[7];
	return max > 0 ? max : 100;
}

static int
ddc_adjust(const char *connector, int delta)
{
	int bus = ddc_find_bus(connector);
	if (bus < 0) {
		fprintf(stderr, "sbrightness: no DDC bus found for %s\n", connector);
		return -1;
	}

	char devpath[32];
	snprintf(devpath, sizeof(devpath), "/dev/i2c-%d", bus);

	int fd = open(devpath, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "sbrightness: cannot open %s: %s\n",
			devpath, strerror(errno));
		return -1;
	}

	int max = ddc_get_max(fd, VCP_BRIGHTNESS);
	int cur = ddc_getvcp(fd, VCP_BRIGHTNESS);
	if (cur < 0) {
		fprintf(stderr, "sbrightness: DDC getvcp failed for %s\n", connector);
		close(fd);
		return -1;
	}

	int next = cur + delta;
	if (next < 0)   next = 0;
	if (next > 100) next = 100;

	int ret = ddc_setvcp(fd, VCP_BRIGHTNESS, next, max);
	close(fd);
	return ret;
}

/* entry point */

static void
usage(void)
{
	fprintf(stderr, "usage: sbrightness <output> <raise|lower|get> [step]\n");
	exit(1);
}

static int
is_internal(const char *name)
{
	return strncmp(name, "eDP",  3) == 0
	    || strncmp(name, "LVDS", 4) == 0;
}

static int
bl_get(const char *connector)
{
	char base[512];
	if (bl_find(connector, base, sizeof(base)) < 0) return -1;

	char path[560];
	snprintf(path, sizeof(path), "%s/max_brightness", base);
	int max = bl_read_int(path);
	if (max <= 0) max = 100;

	snprintf(path, sizeof(path), "%s/brightness", base);
	int cur = bl_read_int(path);
	if (cur < 0) return -1;

	return (cur * 100) / max;
}

static int
ddc_get(const char *connector)
{
	int bus = ddc_find_bus(connector);
	if (bus < 0) return -1;

	char devpath[32];
	snprintf(devpath, sizeof(devpath), "/dev/i2c-%d", bus);

	int fd = open(devpath, O_RDWR);
	if (fd < 0) return -1;

	int val = ddc_getvcp(fd, VCP_BRIGHTNESS);
	close(fd);
	return val;
}

int
main(int argc, char *argv[])
{
	if (argc < 3) usage();

	const char *output = argv[1];
	const char *action = argv[2];
	int step = argc >= 4 ? atoi(argv[3]) : DEFAULT_STEP;

	if (step <= 0) step = DEFAULT_STEP;

	if (strcmp(action, "get") == 0) {
		int val = is_internal(output) ? bl_get(output) : ddc_get(output);
		if (val < 0) { fprintf(stderr, "sbrightness: get failed\n"); return 1; }
		printf("%d\n", val);
		return 0;
	}

	int delta;
	if (strcmp(action, "raise") == 0)      delta = +step;
	else if (strcmp(action, "lower") == 0) delta = -step;
	else usage();

	int ret;
	if (is_internal(output))
		ret = bl_adjust(output, delta);
	else
		ret = ddc_adjust(output, delta);

	if (ret < 0) return 1;

	/* print new brightness so callers don't need a second round trip */
	int val = is_internal(output) ? bl_get(output) : ddc_get(output);
	if (val >= 0) printf("%d\n", val);

	return 0;
}
