@Application
@Capability("system.logging")
@Capability("system.info")
@Capability("port.receive")
class Main {
    static main(): void {
        System.log("gothicOS shell");
        System.log("type help for commands");
        let running: uint32 = 1;
        while (running == 1) {
            System.log("> ");
            let command: uint32 = Port.receive("shell.in");
            if (command == 0) {
                System.log("bye");
                running = 0;
            } else {
                if (command == 1) {
                    System.log("commands: help, info, exit");
                } else {
                    if (command == 2) {
                        System.log(System.info());
                    } else {
                        let text: string = Port.receive("shell.in");
                        System.log(text);
                    }
                }
            }
        }
    }
}
