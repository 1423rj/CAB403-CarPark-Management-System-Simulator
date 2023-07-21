# CAB403-CarPark-Management-System-Simulator
 

# Car Park Simulator

This is a C program that simulates a car park with multiple entrances, exits, and levels. Manage a Carpark including boomgates, Fire, Billing and randomised plates. First attempt using mutex locks etc. The simulator uses shared memory to coordinate communication between different processes representing cars, entrance boom gates, exit boom gates, and temperature sensors.

You will need both simulator and manager runnning at the same time for this to work as intended.
## Requirements

- `inttypes.h`
- `stdbool.h`
- `stdio.h`
- `stdlib.h`
- `string.h`
- `pthread.h`
- `sys/types.h`
- `sys/stat.h`
- `sys/mman.h`
- `fcntl.h`
- `unistd.h`
- `time.h`
- `sharedMemory.h`