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
#define MAX_KEY_DEVS		8

#define ARRAY_SIZE(array)	(sizeof(array) / sizeof(*array))
#define	TEST_BIT(bit, array)	(array[bit / 8] & (1 << (bit % 8)))

/*
 * The struct that contains the necessary data to manage the virtual
 * input device. We currently support a single force feedback device,
 * a single abs device, and multiple key devices.
 */
struct virtual_device {
	struct uinput_setup usetup;
	struct uinput_abs_setup uabssetup[ABS_MAX];
	int uinput_fd;
	int ff_fd;
	int abs_fd;
	int key_fd[MAX_KEY_DEVS];
};

struct dev_info {
	const char name[256];
};

/*
 * List of all the "devices of interest" that we're looking to
 * capture. Only the last ff device, last abs device, and first
 * 10 key devices that match the names below will be used by the
 * driver.
 */
static struct dev_info input_devs[] = {
	{ .name = "pwm-vibrator" },
	{ .name = "adc-joystick" },
	{ .name = "gpio-keys-control" },
	{ .name = "gpio-keys-vol" },
	{ .name = "adc-keys" },
};

/**
 * enumerate_abs_device() - Identify ABS axes and features
 * @v_dev: pointer to virtual_device struct
 *
 * Enumerate ABS axes and add them to the uinput virtual device.
 * Return 0 on success or negative on error.
 */
int enumerate_abs_device(struct virtual_device *v_dev)
{
	uint8_t abs_b[ABS_MAX/8 + 1];
	int ret = 0;

	if (v_dev->abs_fd <= 0)
		return 0;

	ret = ioctl(v_dev->abs_fd,
		    EVIOCGBIT(EV_ABS, sizeof(abs_b)), abs_b);
	if (ret < 0) {
		printf("Unable to enumerate ABS device: %d\n", ret);
		return -ENODEV;
	}

	for (int i = 0; i < ABS_MAX; i++) {
		if (TEST_BIT(i, abs_b)) {
			ret = ioctl(v_dev->abs_fd, EVIOCGABS(i),
				    &v_dev->uabssetup[i].absinfo);
			if (ret)
				continue;

			ret = ioctl(v_dev->uinput_fd,
				    UI_SET_ABSBIT, i);
			if (ret)
				continue;
			v_dev->uabssetup[i].code = i;
			ret = ioctl(v_dev->uinput_fd, UI_ABS_SETUP,
				    &v_dev->uabssetup[i]);
			if (ret)
				printf("Unable to set abs axis %d\n",
				       i);
		}
	}
	return 0;
}

/**
 * enumerate_key_devices() - Identify supported keys
 * @v_dev: pointer to virtual_device struct
 *
 * Enumerate keys and add them to the uinput virtual device. Return
 * number of keys identified.
 */
int enumerate_key_devices(struct virtual_device *v_dev)
{
	uint8_t key_b[KEY_MAX/8 + 1];
	int dev_count = 0;
	int keys = 0;

	for (int dev_num = 0; dev_num < MAX_KEY_DEVS; dev_num++) {
		if (v_dev->key_fd[dev_num] > 0)
			dev_count += 1;
	}

	for (int k = 0; k < dev_count; k++) {
		ioctl(v_dev->key_fd[k],
		      EVIOCGBIT(EV_KEY, sizeof(key_b)), key_b);
		for (int i = 0; i < KEY_MAX; i++) {
			if (TEST_BIT(i, key_b)) {
				ioctl(v_dev->uinput_fd,
				      UI_SET_KEYBIT, i);
				keys += 1;
			}
		}
	}

	return keys;
}

/**
 * iterate_input_devices() - Identify input devices to be monitored
 * @v_dev: pointer to virtual_device struct
 *
 * Iterate over all of the event input devices to find the ones we
 * want to monitor and start adding them to the virtual_device struct.
 * FF devices are closed as read-only and reopened as write-only, since
 * we need to write to them but not necessarily read them. Return is
 * total number of devices found.
 *
 * TODO: Cleanup how we open and add file descriptors to the main
 * struct.
 */
int iterate_input_devices(struct virtual_device *v_dev)
{
	char fd_dev[20];
	char name[256];
	int fd;
	int count = 0;
	int key_devs = 0;
	unsigned long evbit = 0;

	for (int i = 0; i < 256; i++) {
		sprintf(fd_dev, "/dev/input/event%d", i);
		fd = open(fd_dev, O_RDONLY);
		if (fd == -1)
			continue;
		ioctl(fd, EVIOCGNAME(256), name);
		ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
		for (int i = 0; i < ARRAY_SIZE(input_devs); i++) {
			if (!strcmp(name, input_devs[i].name)) {
				if (evbit & (1 << EV_FF)) {
					v_dev->ff_fd = open(fd_dev,
							    O_WRONLY);
					printf("Found EV_FF: %s\n",
					       fd_dev);
					count += 1;
				}
				if (evbit & (1 << EV_ABS)) {
					v_dev->abs_fd = open(fd_dev,
							     O_RDONLY |
							     O_NONBLOCK);
					printf("Found EV_ABS: %s\n",
					       fd_dev);
					count += 1;
				}
				if (evbit & (1 << EV_KEY)) {
					if (key_devs >= MAX_KEY_DEVS)
						continue;
					v_dev->key_fd[key_devs] = open(fd_dev,
								       O_RDONLY |
								       O_NONBLOCK);
					printf("Found EV_KEY: %s\n",
					       fd_dev);
					count += 1;
					key_devs += 1;
				}
			}
		}
		close(fd);
	}

	return count;
}

/**
 * create_uinput_device() - Create a new composite uinput device
 * @v_dev: pointer to virtual input device
 *
 * Create a uinput device for use by userspace to combine all of the
 * monitored input devices into a single one that applications are
 * more capable of dealing with. Return 0 on success, negative on
 * error.
 *
 * TODO: enumerate force feedback capabilites instead of hard-coding.
 */
int create_uinput_device(struct virtual_device *v_dev)
{
	int ret = 0;

	v_dev->uinput_fd = open("/dev/uinput", O_RDWR | O_NONBLOCK |
					       O_DSYNC | O_RSYNC);
	if (v_dev->uinput_fd == -1)
		return -ENODEV;

	if (v_dev->abs_fd > 0) {
		ret = ioctl(v_dev->uinput_fd, UI_SET_EVBIT, EV_ABS);
		if (ret)
			return ret;
		ret = enumerate_abs_device(v_dev);
		if (ret)
			return ret;
	}

	if (v_dev->key_fd[0] > 0) {
		ret = ioctl(v_dev->uinput_fd, UI_SET_EVBIT, EV_KEY);
		if (ret)
			return ret;
		ret = enumerate_key_devices(v_dev);
		if (!(ret > 0)) {
			printf("No keys found\n");
			return -ENODEV;
		}
	}

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
	sprintf(v_dev->usetup.name, DEVICE_NAME);

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
int handle_uinput_ff_upload(struct virtual_device *v_dev,
			    struct input_event ev)
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
int handle_uinput_ff_erase(struct virtual_device *v_dev,
			   struct input_event ev)
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
 * set_ff_gain() - Set gain on physical ff hardware
 * @v_dev: main virtual device struct
 * @gain: value to set for gain
 *
 * Set the gain value of the physical ff hardware based on event
 * received by uinput device. Return value is 0 for success,
 * negative for error.
 */
int set_ff_gain(struct virtual_device *v_dev, __u16 gain)
{
	struct input_event ff_event = {
		.type = EV_FF,
		.code = FF_GAIN,
		.value = gain,
	};
	int ret = 0;

	ret = write(v_dev->ff_fd, (const void *) &ff_event,
		    sizeof(ff_event));

	if (ret != sizeof(ff_event)) {
		printf("Could not set device gain\n");
		return -EIO;
	}

	return 0;
}

/**
 * set_ff_effect_status() - Set ff effect on physical ff hardware
 * @v_dev: main virtual device struct
 * @int: id of effect
 * @status: status of effect - 1 or 0
 *
 * Set the effect status of the physical ff hardware based on event
 * received by uinput device. Return value is 0 for success,
 * negative for error.
 */
int set_ff_effect_status(struct virtual_device *v_dev, int effect,
			 int status)
{
	struct input_event ff_event = {
		.type = EV_FF,
		.code = effect,
		.value = status,
	};
	int ret = 0;

	ret = write(v_dev->ff_fd, (const void *) &ff_event,
		    sizeof(ff_event));
	if (ret != sizeof(ff_event)) {
		printf("Could not set effect status\n");
		return -EIO;
	}

	return 0;
}

/**
 * handle_ff_events() - Respond to ff_events
 *
 * @v_dev: main virtual device struct
 * @ev: input_event initiating ff upload
 *
 * Dispatch an ff event to the correct ff handler. Return value is 0
 * for success, negative for error. For some reason it was insufficient
 * to simply forward the input_event, we had to create a new one.
 */
int handle_ff_events(struct virtual_device *v_dev,
		     struct input_event ev)
{
	int ret = 0;

	if (ev.code == FF_GAIN)
		return set_ff_gain(v_dev, ev.value);

	if (ev.code <= FF_GAIN)
		return set_ff_effect_status(v_dev, ev.code, ev.value);

	return ret;
}

/**
 * parse_ev_incoming() - Process incoming event and hand off to correct
 * helper function.
 *
 * @v_dev: main virtual device struct
 * @fd_in: file descriptor responsible for event
 *
 * Process an EPOLLIN request and hand off necessary data to correct
 * function. Return value is 0 for success, negative for error.
 */
void parse_ev_incoming(struct virtual_device *v_dev, int fd_in)
{
	struct input_event ev;
	int len, ret;

	len = read(fd_in, &ev, sizeof(ev));
	if (len != -1) {
		switch (ev.type) {
		case EV_SYN:
		case EV_ABS:
		case EV_KEY:
			if (v_dev->uinput_fd != fd_in) {
				ret = write(v_dev->uinput_fd,
				      &ev, sizeof(ev));
				if (ret < 0)
					printf("Event dropped\n");
			}
			break;
		case EV_UINPUT:
			if (ev.code == UI_FF_UPLOAD) {
				handle_uinput_ff_upload(v_dev, ev);
				break;
			} else if (ev.code == UI_FF_ERASE) {
				handle_uinput_ff_erase(v_dev, ev);
				break;
			}
			printf("UINPUT ev %d not handled\n", ev.code);
			break;
		case EV_FF:
			if (v_dev->uinput_fd == fd_in)
				handle_ff_events(v_dev, ev);
			break;
		default:
			/* Catch for events we don't support yet */
			printf("EV type %d EV code %d not handled\n",
			       ev.type, ev.code);
		}

	} else {
		printf("read failed descriptor %d, errno %d\n",
		       fd_in, errno);
	}
}

/**
 * define_epoll_fds() - Add all required file descriptors to epoll to
 * be monitored.
 *
 * @v_dev: main virtual device struct
 * @ep_fd: epoll file descriptor
 *
 * Iterate through all file descriptors to monitor and add those which
 * need to be monitored by epoll. At a minimum we need to monitor the
 * uinput device for force feedback effects and at least 1 of either
 * an ABS device or 1 or more key devices.
 */
int define_epoll_fds(struct virtual_device *v_dev, int ep_fd)
{
	struct epoll_event event;
	int ret = 0;

	event.events = EPOLLIN;
	event.data.fd = v_dev->uinput_fd;
	ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, v_dev->uinput_fd,
			&event);
	if (ret == -1) {
		printf("Cannot monitor uinput device\n");
		return -1;
	}

	if (v_dev->abs_fd > 0) {
		event.events = EPOLLIN;
		event.data.fd = v_dev->abs_fd;
		ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, v_dev->abs_fd,
				&event);
		if (ret == -1) {
			printf("Cannot monitor abs device\n");
			return -1;
		}
	}

	for (int i = 0; i < MAX_KEY_DEVS; i++) {
		if (!(v_dev->key_fd[i] > 0))
			continue;
		event.events = EPOLLIN;
		event.data.fd = v_dev->key_fd[i];
		ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, v_dev->key_fd[i],
				&event);
		if (ret == -1) {
			printf("Cannot monitor key device %d\n", i);
			return -1;
		}
	}

	return 0;
}

int main(void)
{
	struct epoll_event event_queue[MAX_EVENTS];
	struct virtual_device *v_dev;
	int ep_fd;
	int ret = 0;

	v_dev = malloc(sizeof(struct virtual_device));
	if (v_dev == NULL) {
		printf("Unable to allocate memory for virtual dev.\n");
		return -ENOMEM;
	};

	memset(v_dev, 0, sizeof(struct virtual_device));

	ret = iterate_input_devices(v_dev);
	if (ret == 0) {
		printf("No input devices found to capture\n");
		return -ENODEV;
	}

	ret = create_uinput_device(v_dev);
	if (ret) {
		printf("Unable to create uinput device: %d\n", ret);
		return -ENODEV;
	}

	ep_fd = epoll_create1(0);
	if (ep_fd == -1) {
		printf("Unable to start epoll\n");
		return -1;
	}

	ret = define_epoll_fds(v_dev, ep_fd);
	if (ret) {
		printf("Cannot monitor input devices: %d\n", ret);
		return ret;
	}

	while (1) {
		int n, i;

		n = epoll_wait(ep_fd, event_queue, 1, -1);
		for (i = 0; i < n; i++) {
			if (event_queue[i].events & EPOLLIN)
				parse_ev_incoming(v_dev,
						  event_queue[i].data.fd);
			else {
				printf("epoll error, type %u\n",
				       event_queue[i].events);
				close(event_queue[i].data.fd);
				continue;
			}
		}
	}
}
