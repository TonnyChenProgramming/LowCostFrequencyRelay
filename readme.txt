Project: Low-Cost Frequency Relay
Authors: Tonny Chen and Pranit
Date: 17/04/2026
Target Platform: Altera/Intel DE2-115 FPGA development kit with Nios II soft-core CPU

1. PROJECT OVERVIEW
This program implements a low-cost frequency relay on the DE2-115 platform. The system continuously monitors the input frequency signal from the frequency analyser peripheral, calculates both frequency and rate of change of frequency (RoCoF), and performs staged load shedding or recovery for up to 5 loads.

The code uses FreeRTOS tasks, queues, interrupts, a 500 ms software timer, VGA display, LEDs, slide switches, push buttons, and PS/2 keyboard input. The default thresholds in the current source code are:
- Frequency threshold: 49.0 Hz
- RoCoF threshold: 8.0 Hz/s

2. REQUIRED HARDWARE / PERIPHERALS
Make sure the following peripherals are included in the programmed DE2-115 system design:
- Nios II soft-core processor
- Frequency analyser peripheral
- 1 us Avalon timer (TIMER1US_BASE)
- Push buttons (PUSH_BUTTON_BASE)
- Slide switches (SLIDE_SWITCH_BASE)
- Red LEDs (RED_LEDS_BASE)
- Green LEDs (GREEN_LEDS_BASE)
- VGA pixel buffer DMA
- VGA character buffer DMA
- PS/2 keyboard interface
- FreeRTOS-compatible BSP

3. IMPORTANT USER INTERFACE MAPPING
The application behavior, based on the source code, is:
- Slide switches: user load request input for up to 5 loads
- Red LEDs: currently active loads
- Green LEDs: shed loads
- Push button: toggles between MAINTENANCE mode and LOAD_MANAGING mode
- PS/2 keyboard:
  - W / w: increase frequency threshold by 0.1 Hz
  - S / s: decrease frequency threshold by 0.1 Hz
  - D / d: increase RoCoF threshold by 0.1 Hz/s
  - A / a: decrease RoCoF threshold by 0.1 Hz/s
- VGA monitor: displays live frequency, RoCoF, thresholds, stability state, recent samples, and timing statistics

4. SOFTWARE STRUCTURE
The code creates the following main FreeRTOS tasks:
- T_FreqAndRoc: converts analyser count into frequency and RoCoF
- T_VgaDisplay: draws plots and status text on the VGA monitor
- T_SwitchPolling: reads slide switch state
- T_StabilityMonitor: compares frequency and RoCoF against thresholds
- T_LoadCtrl: performs load shedding / recovery logic
- T_UpdateRedLed: updates active-load LEDs
- T_UpdateGreenLed: updates shed-load LEDs
- T_UpdateThreshold: handles keyboard threshold updates

The code also initializes:
- push-button interrupt
- frequency analyser interrupt
- PS/2 keyboard interrupt
- a periodic 500 ms FreeRTOS software timer used by load management logic
- a 1 us hardware timer used for response-time measurement

5. BEFORE YOU RUN
Before compiling or downloading the program, confirm the following:

5.1 Hardware design
- The FPGA design loaded onto the DE2-115 must contain all peripherals referenced by system.h.
- The base names used in the code must match the Platform Designer / Qsys system.
- The generated system.h file must define symbols such as:
  - FREQUENCY_ANALYSER_BASE
  - TIMER1US_BASE
  - PUSH_BUTTON_BASE
  - SLIDE_SWITCH_BASE
  - RED_LEDS_BASE
  - GREEN_LEDS_BASE
  - PS2_BASE

5.2 BSP and libraries
- The Nios II BSP must include FreeRTOS.
- The BSP must also include the Altera University Program VGA and PS/2 drivers used by the code.
- Make sure the include paths resolve these headers:
  - altera_up_avalon_video_character_buffer_with_dma.h
  - altera_up_avalon_video_pixel_buffer_dma.h
  - altera_up_avalon_ps2.h
  - altera_up_ps2_keyboard.h
  - freertos/FreeRTOS.h
  - freertos/task.h
  - freertos/queue.h
  - freertos/semphr.h

5.3 Physical setup
- Connect a VGA monitor to the DE2-115 board.
- Connect a PS/2 keyboard to the board.
- Make sure the frequency analyser input is connected to a valid signal source.
- Make sure the board is powered and the USB-Blaster connection is working.

6. HOW TO BUILD THE SOFTWARE
Use the Nios II SBT / Eclipse workflow or the equivalent build flow for your course setup.

Step 1: Open the Nios II software workspace.
Step 2: Import or open the application project containing this C source file.
Step 3: Import or open the BSP project linked to the correct .sopcinfo file.
Step 4: Regenerate the BSP if the hardware system has changed.
Step 5: Clean both the BSP and application projects.
Step 6: Build the BSP project first.
Step 7: Build the application project.

If there are build errors, check:
- missing BSP driver support
- mismatched peripheral names in system.h
- incorrect FreeRTOS include paths
- missing university program display or PS/2 drivers

7. HOW TO PROGRAM THE DEVELOPMENT KIT
Step 1: Compile the FPGA hardware design in Quartus.
Step 2: Program the DE2-115 with the correct .sof file using Quartus Programmer.
Step 3: After FPGA configuration is successful, open the Nios II software tool.
Step 4: Download the compiled .elf application to the Nios II CPU.
Step 5: Run the program.

8. HOW TO OPERATE THE SYSTEM ON THE BOARD
After the program starts:

8.1 Boot behavior
- The program initializes thresholds to 49.0 Hz and 8.0 Hz/s.
- All red and green LEDs are cleared at startup.
- Interrupts, queues, timers, semaphores, and tasks are created.
- The FreeRTOS scheduler starts.

8.2 Maintenance mode
- In MAINTENANCE mode, the output depends directly on slide switches.
- Loads are controlled only by the switch positions.
- No automatic shedding should be applied.

8.3 Load-managing mode
- In LOAD_MANAGING mode, the system monitors stability continuously.
- If frequency falls below threshold, or absolute RoCoF exceeds threshold, the network is considered unstable.
- When unstable, the controller starts shedding loads.
- When stable for the required interval, the controller begins recovering loads.

8.4 Push button use
- Press the push button to toggle between MAINTENANCE and LOAD_MANAGING.
- The software prints the selected mode to the console.

8.5 Slide switch use
- Use the lower 5 slide switches to request up to 5 loads.
- These switches determine the user-selected load mask.

8.6 LED meaning
- Red LEDs show the effective active loads.
- Green LEDs show the loads that have been shed by control logic.

8.7 Keyboard tuning
Use the PS/2 keyboard while the program is running:
- W / w -> frequency threshold +0.1 Hz
- S / s -> frequency threshold -0.1 Hz
- D / d -> RoCoF threshold +0.1 Hz/s
- A / a -> RoCoF threshold -0.1 Hz/s

The code limits thresholds to:
- Frequency threshold: 45.0 Hz to 50.0 Hz
- RoCoF threshold: 0.1 Hz/s to 50.0 Hz/s

8.8 VGA output
The VGA monitor displays:
- frequency plot
- RoCoF plot
- current thresholds
- stable / unstable status
- recent frequency values
- response-time statistics
- elapsed runtime

9. EXPECTED EXECUTION FLOW
1. Frequency analyser interrupt places ADC count into a queue.
2. T_FreqAndRoc converts count to frequency and RoCoF.
3. T_StabilityMonitor compares values against thresholds.
4. T_LoadCtrl updates the shed/recover state.
5. LED tasks update the board LEDs.
6. T_VgaDisplay refreshes on-screen plots and system status.
7. T_UpdateThreshold reacts to PS/2 keyboard input.

10. HOW TO VERIFY CORRECT OPERATION
To test the system:

Test A: VGA and boot check
- Program the FPGA and run the ELF.
- Confirm VGA output appears.
- Confirm no peripheral open errors appear on the console.

Test B: Switch / LED check
- In MAINTENANCE mode, toggle slide switches.
- Confirm the red LEDs follow the switch state.

Test C: Mode toggle check
- Press the push button.
- Confirm the console prints MAINTENANCE or LOAD_MANAGING.

Test D: Threshold update check
- Press W/S/A/D on the PS/2 keyboard.
- Confirm the console shows updated threshold values.
- Confirm the VGA threshold text changes accordingly.

Test E: Load-shedding check
- Feed a frequency signal that causes low frequency or large RoCoF.
- Confirm the system marks the network unstable.
- Confirm loads begin to shed and LEDs change accordingly.

Test F: Recovery check
- Return the input signal to a stable condition.
- Confirm loads recover after the programmed timing condition.

11. NOTES / ASSUMPTIONS
- This README is written from the current source code behavior.
- The code assumes a working frequency analyser peripheral and valid signal source.
- The code supports up to 5 loads.
- The code uses a 500 ms FreeRTOS timer for management timing.
- The code measures response time using the 1 us timer snapshot registers.
- The exact push button index is determined by the board design; the ISR currently enables all push-button interrupt bits.

12. COMMON ISSUES
Issue: VGA stays blank
- Check that the VGA DMA peripherals exist in hardware.
- Check the monitor cable and resolution support.
- Check that the board is programmed with the correct hardware design.

Issue: PS/2 keyboard does not respond
- Check that the PS/2 peripheral exists in hardware.
- Check the keyboard connection.
- Check that PS2_BASE and PS2_IRQ match the hardware design.

Issue: LEDs do not change
- Check RED_LEDS_BASE and GREEN_LEDS_BASE.
- Confirm the scheduler has started.
- Confirm T_LoadCtrl, T_UpdateRedLed, and T_UpdateGreenLed are running.

Issue: No frequency data
- Check that the frequency analyser peripheral is generating interrupts.
- Check the signal source wired to the analyser.
- Check FREQUENCY_ANALYSER_BASE and FREQUENCY_ANALYSER_IRQ.

Issue: Software build fails
- Regenerate BSP.
- Check include paths and driver availability.
- Make sure the BSP and application match the current .sopcinfo design.

13. SUMMARY
This application is intended to be run on a DE2-115 development board with a Nios II system that includes VGA, PS/2, timer, LED, switch, push-button, and frequency analyser peripherals. Once programmed, the system monitors power-system frequency conditions, displays live information on VGA, and performs automatic load shedding and recovery for up to five loads.
