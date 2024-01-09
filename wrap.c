// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handheld device wrapper userspace helper
 *
 * Copyright (c) 2024 Chris Morgan <macromorgan@hotmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#define DEVICE_NAME		"Virtual Gamepad"
#define DEVICE_VID		0x1234
#define DEVICE_PID		0x5678

#define MAX_EVENTS		64

/* Value comes from kernel define FF_MEMLESS_EFFECTS which is 16 */
#define MAX_FF_EFFECTS		16

#define ARRAY_SIZE(array)	(sizeof(array) / sizeof(*array))

/*
 * The struct that contains the necessary data to manage the virtual
 * input device.
 */
struct virtual_device {
	struct uinput_setup usetup;
	int uinput_fd;
	int ff_fd;
};

struct dev_info {
	const char name[256];
};

/*
 * List of all the "devices of interest" that we're looking to
 * capture. As of right now though we are only playing with the
 * pwm-vibrator.
 */
static struct dev_info input_devs[] = {
	{ .name = "pwm-vibrator" },
	{ .name = "adc-joystick" },
	{ .name = "gpio-keys-control" },
	{ .name = "gpio-keys-vol" },
	{ .name = "adc-keys" },
};

/**
 * iterate_input_devices() - Identify input devices to be monitored
 * @v_dev: pointer to virtual_device struct
 *
 * Iterate over all of the event input devices to find the ones we
 * want to monitor and start adding them to the virtual_device struct.
 * FF devices are closed as read-only and reopened as read/write, since
 * we need to communicate bidirectionally with them.
 */
void iterate_input_devices(struct virtual_device *v_dev)
{
	char fd_dev[20];
	char name[256];
	int fd, ret;
	unsigned long evbit = 0;

	for (int i = 0; i < 256; i++) {
		sprintf(fd_dev, "/dev/input/event%d", i);
		fd = open(fd_dev, O_RDONLY);
		if (fd == -1)
			continue;
		ret = ioctl(fd, EVIOCGNAME(256), name);
		ret = ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
		for (int i = 0; i < ARRAY_SIZE(input_devs); i++) {
			if (!strcmp(name, input_devs[i].name) &&
					 (evbit & (1 << EV_FF)))
				v_dev->ff_fd = open(fd_dev, O_RDWR |
							    O_NONBLOCK |
							    O_DSYNC |
							    O_RSYNC);
		};
		close(fd);
	}
}

/**
 * create_uinput_device() - Create a new composite uinput device
 * @v_dev: pointer to virtual input device
 *
 * Create a uinput device for use by userspace to combine all of the
 * monitored input devices into a single one that applications are
 * more capable of dealing with. Return 0 on success, negative on
 * error.
 */
int create_uinput_device(struct virtual_device *v_dev)
{
	int ret = 0;

	v_dev->uinput_fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_DSYNC | O_RSYNC);
	if (v_dev->uinput_fd == -1)
		return -ENODEV;

	if (v_dev->ff_fd > 0) {
		ret = ioctl(v_dev->uinput_fd, UI_SET_EVBIT, EV_FF);
		if (ret)
			return ret;

		ioctl(v_dev->uinput_fd, UI_SET_FFBIT, FF_RUMBLE);
		ioctl(v_dev->uinput_fd, UI_SET_FFBIT, FF_GAIN);
		ioctl(v_dev->uinput_fd, UI_SET_FFBIT, FF_PERIODIC);
		ioctl(v_dev->uinput_fd, UI_SET_FFBIT, FF_SINE);
		ioctl(v_dev->uinput_fd, UI_SET_FFBIT, FF_TRIANGLE);
		ioctl(v_dev->uinput_fd, UI_SET_FFBIT, FF_SQUARE);

		v_dev->usetup.ff_effects_max = MAX_FF_EFFECTS;
	}

	v_dev->usetup.id.bustype = BUS_HOST;
	v_dev->usetup.id.vendor = DEVICE_VID;
	v_dev->usetup.id.product = DEVICE_PID;
	strncpy(v_dev->usetup.name, DEVICE_NAME, sizeof(DEVICE_NAME));

	ret = ioctl(v_dev->uinput_fd, UI_DEV_SETUP, &v_dev->usetup);
	if (ret)
		return ret;

	ret = ioctl(v_dev->uinput_fd, UI_DEV_CREATE);
	if (ret)
		return ret;

	return 0;
}

/**
 * handle_uinput_ff_upload() - Capture and respond to ff_upload
 * requests
 * @v_dev: main virtual device struct
 * @ev: input_event initiating ff upload
 *
 * Handle the necessary IOCTLs used for processing an incoming ff
 * effect upload. Read the upload request from the uinput device and
 * replay it to the physical ff device. Read the response from the
 * physical ff device and replay it back to the uinput device.
 * Return value is 0 for success, negative for error.
 */
int handle_uinput_ff_upload(struct virtual_device *v_dev, struct input_event ev)
{
	struct uinput_ff_upload ff_payload;
	struct ff_effect effect;
	int ret = 0;

	ff_payload.request_id = ev.value;
	ret = ioctl(v_dev->uinput_fd, UI_BEGIN_FF_UPLOAD, &ff_payload);
	if (ret)
		return ret;
	effect = ff_payload.effect;
	effect.id = -1;

	ret = ioctl(v_dev->ff_fd, EVIOCSFF, &effect);
	if (ret)
		return ret;
	ff_payload.retval = ret;

	ret = ioctl(v_dev->uinput_fd, UI_END_FF_UPLOAD, &ff_payload);
	if (ret)
		return ret;

	return 0;
}


/**
 * handle_uinput_ff_erase() - Capture and respond to ff_erase
 * requests
 * @v_dev: main virtual device struct
 * @ev: input_event initiating ff erase
 *
 * Handle the necessary IOCTLs used for processing an incoming ff
 * effect erase. Read the erase request from the uinput device and
 * replay it to the physical ff device. Read the response from the
 * physical ff device and replay it back to the uinput device.
 * Return value is 0 for success, negative for error.
 */
int handle_uinput_ff_erase(struct virtual_device *v_dev, struct input_event ev)
{
	struct uinput_ff_erase ff_payload;
	int ret = 0;

	ff_payload.request_id = ev.value;
	ret = ioctl(v_dev->uinput_fd, UI_BEGIN_FF_ERASE, &ff_payload);
	if (ret)
		return ret;

	ret = ioctl(v_dev->ff_fd, EVIOCRMFF, ff_payload.effect_id);
	if (ret)
		return ret;
	ff_payload.retval = ret;

	ret = ioctl(v_dev->uinput_fd, UI_END_FF_ERASE, &ff_payload);
	if (ret)
		return ret;

	return 0;
}

/**
 * handle_ff_events() - Respond to ff_events
 *
 * @v_dev: main virtual device struct
 * @ev: input_event initiating ff upload
 * @fd_in: file descriptor responsible for ff_event
 *
 * Replay an ff event to both the physical and virtual ff device.
 * Return value is 0 for success, negative for error.
 */
int handle_ff_events(struct virtual_device *v_dev, struct input_event ev, int fd_in)
{
	struct input_event ff_event;
	int ret = 0;

	if (ev.type != EV_FF)
		return 0;

	memset(&ff_event, 0, sizeof(ff_event));
	ff_event.type = ev.type;
	ff_event.code = ev.code;
	ff_event.value = ev.value;
	ret += write(v_dev->uinput_fd, (const void *) &ff_event, sizeof(ff_event));
	ret += write(v_dev->ff_fd, (const void *) &ff_event, sizeof(ff_event));
	if (ret != sizeof(ff_event) * 2) {
		printf("Unable to handle ff event: %d, %d\n", ret, sizeof(ff_event));
		return ret;
	}

	return 0;
}
/**
 * parse_ev_incoming() - Process incoming event and hand off to correct
 * helper function.
 *
 * @v_dev: main virtual device struct
 * @fd_in: file descriptor responsible for event
 *
 * Process an EPOLLIN queue item and hand off necessary data to correct
 * function. Return value is 0 for success, negative for error.
 */
void parse_ev_incoming(struct virtual_device *v_dev, int fd_in)
{
	struct input_event ev;
	int len;

	len = read(fd_in, &ev, sizeof(ev));
	if (len != -1) {
		printf("len: %d\n", len);
		printf("Type %d Code %d Value %d Time %lu source %d\n",
			ev.type,
			ev.code,
			ev.value,
			ev.time.tv_sec,
			fd_in);
		if ((ev.type == EV_UINPUT) && (ev.code == UI_FF_UPLOAD))
			handle_uinput_ff_upload(v_dev, ev);
		if ((ev.type == EV_UINPUT) && (ev.code == UI_FF_ERASE))
			handle_uinput_ff_erase(v_dev, ev);
		if (ev.type == EV_FF)
			handle_ff_events(v_dev, ev, fd_in);
	} else {
		printf("read failed descriptor %d, errno %d\n", fd_in, errno);
	}
}

int main(void)
{
	struct epoll_event uevent, fevent, event_queue[MAX_EVENTS];
	struct virtual_device *v_dev;
	int ep_fd;
	int ret;

	v_dev = malloc(sizeof(v_dev));

	if (v_dev == NULL) {
		printf("Unable to allocate memory for virtual dev.\n");
		return -ENOMEM;
	};

	iterate_input_devices(v_dev);

	ret = create_uinput_device(v_dev);
	if (ret) {
		printf("Unable to create uinput device\n");
		return -ENODEV;
	}

	ep_fd = epoll_create1(0);
	if (ep_fd == -1) {
		printf("Unable to start epoll\n");
		return -1;
	}

	uevent.events = EPOLLIN | EPOLLET;
	uevent.data.fd = v_dev->uinput_fd;
	ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, v_dev->uinput_fd, &uevent);
	if (ret == -1) {
		printf("Cannot monitor uinput device.\n");
		return -1;
	}

	fevent.events = EPOLLIN | EPOLLET;
	fevent.data.fd = v_dev->ff_fd;
	ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, v_dev->ff_fd, &fevent);
	if (ret == -1) {
		printf("Cannot monitor ff device.\n");
		return -1;
	}

	while (1) {
		int n, i;

		n = epoll_wait(ep_fd, event_queue, MAX_EVENTS, -1);
		for (i = 0; i < n; i++) {
			if (event_queue[i].events & EPOLLIN)
				parse_ev_incoming(v_dev, event_queue[i].data.fd);
			else {
				printf("epoll error, type %lu\n",
				       event_queue[i].events);
				close(event_queue[i].data.fd);
				continue;
			}
		}
	}
}
