/**
 * gothicOS UmbraTS OS-module declarations.
 * These are SDK declarations for the compiler; they are not runtime JS files.
 * Imports are statically linked to the native IDs in os_modules.hpp.
 */

declare module "@gothic/core" {
    export const VERSION: string;
}

declare module "@gothic/system" {
    export const System: {
        log(message: string): void;
        yield(): void;
        info(): number;
        sleep(milliseconds: number): void;
        uptime(): number;
        exit(code: number): void;
    };
}

declare module "@gothic/memory" {
    export const Memory: {
        alloc(size: number, flags: number): number;
        free(handle: number, flags: number): void;
        stats(): number;
    };
}

declare module "@gothic/io" {
    export const IO: {
        read(handle: number, length: number): string;
        write(handle: number, data: string): number;
    };
}

declare module "@gothic/fs" {
    export const FileSystem: {
        open(path: string, flags: number): number;
        read(handle: number, length: number): number[];
        write(handle: number, data: string): number;
        close(handle: number): void;
        stat(path: string): number;
    };
}

declare module "@gothic/network" {
    export const Network: {
        status(): number;
        connect(configuration: string): number;
        send(socket: number, data: string): number;
        receive(socket: number, maximumBytes: number): string;
        close(socket: number): void;
    };
}

declare module "@gothic/async" {
    export const Async: {
        yield(): void;
        sleep(milliseconds: number): void;
        cancel(taskId: number): boolean;
    };
}

declare module "@gothic/device" {
    export const Device: {
        query(deviceId: number): number;
        invoke(deviceId: number, command: number, args: string): number;
    };
}

declare module "@gothic/logging" {
    export const Logging: {
        debug(message: string): void;
        info(message: string): void;
        warn(message: string): void;
        error(message: string): void;
    };
}

declare module "@gothic/math" {
    export const Math: {
        abs(value: number): number;
        min(a: number, b: number): number;
        max(a: number, b: number): number;
    };
}

// TODO: add TypeScript/UmbraTS support to InterfaceKit
// declare module "@interfacekit/app" {
//	export const App: {
//		title(value: string): void;
//		size(height: number, width: number): number;
//		type(value: string): void;
//		content(value: function): void;
//	}
// }
