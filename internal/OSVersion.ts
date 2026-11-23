import * from "@gothic/core";

@Application
@Capability("system.logging")
@Capability("system.info")
export const OSVersion = "0.1.00.1";
export const OSKernelSupported = {
	"kernel": [
		{
		"name": "linux",
		"version": "6.6.162-lts"
		},
		{
			"name": "null_kernel",
			"version": "${OSVersion}"
		}
	],
}

export function OSInfoSpitout() {
	System.log("System Version: ", OSVersion);
	System.log("Kernel Version: ", kerneldetect(OSKernelSupported, version));
	return OSInfo;
}
