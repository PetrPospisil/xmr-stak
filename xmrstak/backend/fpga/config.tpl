R"===(// generated by XMRSTAK_VERSION

/*
 * FPGA configuration. You should play around with threads as the fastest settings will vary.
 * comport       - FPGA COM port number.
 * threads       - Number of FPGA threads (nothing to do with CPU threads).
 *
 * On the first run the miner will look at your system and suggest a basic configuration that will work,
 * you can try to tweak it from there to get the best performance.
 *
 * A filled out configuration should look like this:
 * "fpga_devices_conf" :
 * [
 *     { "comport" : 0, "threads" : 1
 *     },
 * ],
 * If you do not wish to mine with your FPGA(s) then use:
 * "fpga_devices_conf" :
 * null,
 */

"fpga_devices_conf" :
[
FPGACONFIG
],
)==="
