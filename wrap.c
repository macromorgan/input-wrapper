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

#define DEVICE_NAME	"Virtual Gamepad"
#define MAX_EVENTS	32

/**
 * iterate_input_devices() - Identify input devices to be monitored
 * @void: nothing yet
 *
 * Iterate over all of the event input devices to find the ones we
 * want to monitor. No return value as of yet.
 */
void iterate_input_devices(void)
{
	char fd_dev[20];
	char name[256];
	int fd, ret;

	for (int i = 0; i < 256; i++) {
		sprintf(fd_dev, "/dev/input/event%d", i);
		fd = open(fd_dev, O_RDONLY);
		if (fd == -1)
			continue;
		ret = ioctl(fd, EVIOCGNAME(256), name);
		printf("IOCTL name is %s\n", name);
		close(fd);
	}
}

/**
 * create_uinput_device() - Create a new composite uinput device
 * @void: nothing yet
 *
 * Create a uinput device for use by userspace to combine all of the
 * monitored input devices into a single one that applications are
 * more capable of dealing with. Return value is the file descriptor
 * to the newly created uinput device.
 */
int create_uinput_device(void)
{
	struct uinput_setup usetup;
	int ufd;

	ufd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
	ioctl(ufd, UI_SET_EVBIT, EV_FF);
	ioctl(ufd, UI_SET_FFBIT, FF_RUMBLE);
	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_HOST;
	usetup.id.vendor = 0x1234;
	usetup.id.product = 0x5678;
	usetup.ff_effects_max = 10;
	strncpy(usetup.name, "Virtual Gamepad", sizeof(DEVICE_NAME));
	ioctl(ufd, UI_DEV_SETUP, &usetup);
	ioctl(ufd, UI_DEV_CREATE);

	return ufd;
}

/**
 * handle_uinput_ff_upload() - Capture and respond to ff_upload
 * requests
 * @ufd: uinput file descriptor
 * @ff_payload: uinput_ff_upload struct to receive uploaded ff payload.
 * @ev: input_event initiating ff upload.
 *
 * Handle the necessary IOCTLs used for processing an incoming ff
 * effect upload. Return value is 0 for success, negative for error.
 */
int handle_uinput_ff_upload(int ufd, struct uinput_ff_upload *ff_payload,
			    struct input_event ev)
{
	int ret = 0;

	ff_payload->request_id = ev.value;
	ret = ioctl(ufd, UI_BEGIN_FF_UPLOAD, ff_payload);
	if (ret)
		return ret;
	ff_payload->retval = 0;
	ioctl(ufd, UI_END_FF_UPLOAD, ff_payload);
	if (ret)
		return ret;

	return 0;
}

int main(void)
{
	struct epoll_event event;
	struct epoll_event *event_queue;
	struct uinput_ff_upload ff_payload;
	int input_fd, ufd;
	int ep_fd;
	int ret;

	ufd = create_uinput_device();

	ep_fd = epoll_create1(0);
	if (ep_fd == -1) {
		printf("Unable to create epoll\n");
		return -1;
	}

	event.events = EPOLLIN | EPOLLET;
	ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, ufd, &event);
	if (ret == -1) {
		printf("epoll ctl error\n");
		return -1;
	}

	event_queue = calloc(MAX_EVENTS, sizeof(event));

	while (1) {
		int n, i;

		n = epoll_wait(ep_fd, event_queue, MAX_EVENTS, -1);
		for (i = 0; i < n; i++) {
			if (event_queue[i].events & EPOLLIN) {
				int len = 0;
				struct input_event ev;

				printf("EPOLLIN\n");
				len = read(ufd, &ev, sizeof(ev));
				if (len != -1) {
					printf("len: %d\n", len);
					printf("Type %d Code %d Value %d Time %lu\n",
					       ev.type,
					       ev.code,
					       ev.value,
					       ev.time.tv_sec);
					if ((ev.type == EV_UINPUT) &&
					    (ev.code == UI_FF_UPLOAD)) {
						handle_uinput_ff_upload(ufd,
									&ff_payload,
									ev);
					}
				}
			}

			else {
				printf("epoll error, type %lu\n",
				       event_queue[i].events);
				close(event_queue[i].data.fd);
				continue;
			}
		}
	}
}
