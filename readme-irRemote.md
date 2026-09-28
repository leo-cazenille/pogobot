# IR Remote
To control each robot, two ways are possible: by cable (debug mode) or by IR.
In order to control robots by IR, you need to create a IR remote.

## Hardware
Please refer to the section "IR Remote" [here](/Hardware/readme.md). 

## Software
In order to turn a head into a remote control, we need to put a special user code.

    ./pogosoc.py --target=pogobotv3 --cpu-variant=lite --build --remocon

Connect to the robot with the command

    ./litex_term.py --serial-boot --images images.json --safe /dev/ttyUSBX

Type the command

    serialboot

in order to upload the code. The robot reboot on the remote control code.


## Usage

Connect the remote from the folder where the usercode was compiled.

    make connect (TTY=/dev/ttyUSBX) (tty of the remote)

When a remote starts, it exposes the standard pogobot bios. You need to start the remote bios by typing : 

    run

A remote can't execute the command "run" and the command "serialboot" has a different meaning.
Once started different new commands are avaible :

- rc_start : start the user code on the robot (continious).
- rc_stop  : stop the user code and restart inside the pogobot bios (continious).
- rc_erase : erase the user code on the robot (continious).
- rc_send_bios_cmd <cmd> <args> : send the pogobios command by IR (once). 
- rc_send_bios_cmd_cont <delay> <cmd> <args> : send the pogobios command by IR every delay Ms (continious). 
- rc_send_user_msg <msg> : send the message by IR. It is not interpreted by the pogobios. The message type is 1. 
- rc_send_user_msg_cont <delay> <msg> : send the message by IR every delay Ms. It is not interpreted by the pogobios. The message type is 1 (continious). 
- rc_flash_robot ( old serialboot) : send the user code to the robots depending on the folder (once).  

To program a robot by IR, you need to clean the robots and reprogram its.<br>
Reboot the robot. The robots blink slowly green if a program is available. (if it is blue, no program available, no erase needed)<br>
"rc_erase" will erase the program and make its blink blue slowly. <br>
The command "rc_flash_robot" send to the robots a special command to switch to listen mode. Robots blink blue rapidily. <br>
When the programmation is done, the robots blink green slowly. <br>
If the robots blink orange, the programmation is partiel. Change the distance or angle of the remote and start again the "rc_flash_robot" command.

### Versioned IR firmware upload (Phase 2)

Install matching current Pogobios builds on the remote and robots before using
this mode. The current version accepts one image per transfer in the gateware
slot (`0x240000`) or firmware slot (`0x260000`), up to 128 KiB. A two-entry
`images.json` is not supported by `--ir-v2`; upload each image separately.

For a firmware image, connect to the remote with:

    ./litex_term.py --serial-boot --kernel=firmware.bin --kernel-adr=0x260000 --safe --ir-v2 /dev/ttyUSBX

Then enter `rc_flash_robot` at the remote prompt. The PC sends a versioned
START, numbered 64-byte DATA chunks, and END. The robot erases the destination
at START, ignores duplicate chunks, reads each write back, and checks a CRC-32
over the exact image before writing `FlashIsOK`. A robot that misses chunks
keeps the image invalid. A new transfer ID restarts and erases the image;
repeated START with the same ID and metadata preserves progress in RAM until
reboot. The remote currently pauses three seconds after START for flash erase;
this delay needs measurement on hardware.

The serial acknowledgements confirm processing by the remote, not successful
reception or completion by each robot. Check the robot's status before running
the new image. The default terminal mode remains the legacy upload protocol.
