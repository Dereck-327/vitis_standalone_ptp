

# xilinx vitis中lwip适配ptp

## 说明

适用于xilinx裸机，lwip211，项目中添加了ptp over udp 和 gptp over ethernet两种协议，但是都只是做slave，并没有bmc主时钟算法。补充说明见[ptp](https://dereck-327.github.io/2025/09/19/Vivado-Vitis-Platform-Vitis-ptp-lwip-transplantation/)