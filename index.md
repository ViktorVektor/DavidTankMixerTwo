## Fish Tank Solution Mixer v2

![image](https://user-images.githubusercontent.com/41247872/148147411-b339b851-8c2c-4e1a-be0a-d65fddac4911.png)

The second iteration of the mini-project, purpose built for mixing a vat of calcium carbonate solution. The device fits onto the lid of the vat, where a mixing head dips into the solution. A time interval for its sleep and mixint time can be set. For this iteraiton, the speed of the mixing and the method of mixing can also be set. These settings are also now saved even when the device is powered off.

### Usage
 
 Power: USB Micro B, at least 5V 200mA DC, USB from a common phone charger would suffice.
 
 To turn on the device, simply plug in the USB. There is no power switching as a battery source is not included. When powered, the screen will turn on. Use the knob on the lid to select through the options:
 
  - Sleep time: Time interval between mixes.
  - Mix time:   Length of time to mix.
  - Mix speed:  Rotational speed for mixing by adjusting the motor PWM.
  - Mix mode:   The rotation method for mixing.

Settings will be saved between power cycles, but will still need to click through all the options. Note that for sleep time, it will exactly be what was set by the user, unlike in v1 where the mix time was subtracted from the sleep time. The device will go into a mix mode right away, then go to sleep. The display will show a message while asleep.

### The Problem

An alkaline solution of calcium carbonate is used for the operation of a tropical fish tank. However, the solution becomes saturated with extra particulates settling at the bottom. 

### Feedback from v1

The v1 solution mixer operated for about 3 months. It was reported that the motor had ceased to spin, but the device was still powered on.  It was concluded that the mode of failure was the modified servo motor burning out. FOr the power, the user found it best to only use USB power and did not use battery power. Another point of concern was the timing of the sleep intervals, where using the Arduino's internal timer was not accurate enough for both sleeping and mixing. A pilot light was desired as it was difficult to tell whether the device was powered on or not when asleep.

### Design constraints

Learning from v1, the goal of this iteration will be to address the issues encountered.






