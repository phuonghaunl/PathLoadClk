PathLoad+Clk: An improved Available Bandwidth Estimation Tool (ABET) using external clock server, and adopting new smoothing technique calledd BASS.

To compile sender or clock: 

gcc -o sender.c 

gcc -o clock.c 

To compile receiver: gcc -o rcv.c -lm

To run: ./rcv <clock_IP> <wait_thres> <clock_enable> <bass_enable> <num_clock_packets> <r2_thres>

Options: 

+ wai_thres: sleep time to simulate VM scheduling (microseconds) 0 to disable VM scheduling

+ clock_enable:
  1 to enable external clock and use clock timestamps to estimate AB
  0 to disable external clock
+ bass_enable:
  1: to enable BASS (smoothing technique)
  0: to disbale BASS

+ r2_thres: to minimize the overhead of sending clock packets 
  0.01: default, works well for most cases with small overhead.
