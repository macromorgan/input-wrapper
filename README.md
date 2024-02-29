# Virtual Controller

Virtual Controller is a simple C program to emulate to userspace a controller composed of a specific set of input devices. The input devices are identified automatically and enumerated providing their functionality to the composite device. Currently supported is a single force-feedback device, up to 8 abs devices, and up to 8 key devices.

## Building
There are currently no libraries required to compile or use the program, it is built entirely utilizing existing kernel features such as uinput (CONFIG_INPUT_UINPUT), evdev (CONFIG_INPUT_EVDEV), and epoll (CONFIG_EPOLL). These kernel options are usually default.

To compile, simply run make.
```bash
make
```

## Installation

After compilation, install the program.

```bash
sudo make install
```

## Usage

```bash
virtual_controller &
```

The program must be running in the background in order to redirect inputs to the newly created virtual controller. The user should have permission to both access input and uinput devices, and the kernel should support UINPUT, EVDEV, and EPOLL.

## Contributing

Pull requests are welcome. Code must follow the [Linux Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html). While I reserve the right to revisit the decision, it is my expectation that no external libraries should be used; this is to ensure maximum portability in the solution.

## License

[GPLv2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
