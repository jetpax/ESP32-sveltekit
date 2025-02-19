<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { socket } from '$lib/stores/socket';
  // window.socket = socket
  // console.log("Global Socket initialized:", socket);
  import { Terminal } from '@xterm/xterm';
  import { FitAddon } from '@xterm/addon-fit';
  import '@xterm/xterm/css/xterm.css';  // Import default xterm styles

  let term;
  let termContainer;
  let fitAddon;

  const banner = [
    "\x1b[38;05;208;1m            ____       __           _    ____  ________     ",
    "       ___ / __ \\___  / /__________| |  / /  |/  / ___/ ___",
    "     ____ / /_/ / _ \\/ __/ ___/ __ \\ | / / /|_/ /\\__ \\/_____",
    "   _____ / _, _/  __/ /_/ /  / /_/ / |/ / /  / /___/ /_______ ",
    "        /_/ |_|\\___/\\__/_/   \\____/|___/_/  /_//____/       ",
    "",
    "\x1b[0;37mVisit: \x1b[1;32mhttps://retrovms.com\x1b[0m\r\n",
  ];

  onMount(() => {
    term = new Terminal({
      cursorBlink: true,
      theme: {
        background: '#2e2e2e',
        foreground: '#00ff00',
      }
    });

    fitAddon = new FitAddon();
    term.loadAddon(fitAddon);
    term.open(termContainer);
    fitAddon.fit();

    // Display the banner line by line to prevent misalignment
    banner.forEach(line => term.writeln(line));
    term.write('>>> '); // Start the input prompt

    let commandBuffer = '';

    // Handle input from the terminal
  //   term.onData((data) => {
  //   if (data === '\r') {
  //     console.log('Sending command:', commandBuffer);
  //     socket.sendEvent('repl', { command: commandBuffer }); // Send as JSON object      commandBuffer = '';
  //     term.write('\r\n>>> ');
  //   } else {
  //     commandBuffer += data;
  //     term.write(data);
  //   }
  // });

  term.onData((data) => {
  if (data === '\r') { // Enter key
    console.log('Sending command:', JSON.stringify(commandBuffer));
    socket.sendEvent('repl', { command: commandBuffer.trim() }); // Send JSON object
    commandBuffer = ''; // 🔹 Clear buffer after sending
    term.write('\r\n>>> ');
  } else if (data === '\u007F') { // Backspace
    if (commandBuffer.length > 0) {
      commandBuffer = commandBuffer.slice(0, -1);
      term.write('\b \b'); // Remove last character visually
    }
  } else {
    commandBuffer += data;
    term.write(data);
  }
});


    // Listen for responses from the backend
    socket.on('repl', (result) => {
      term.writeln(result);  // Display the result in the terminal
      term.write('>>> ');
    });

    // Resize the terminal when the window size changes
    const resizeHandler = () => fitAddon.fit();
    window.addEventListener('resize', resizeHandler);

    return () => {
      window.removeEventListener('resize', resizeHandler);
      term.dispose();
    };
  });
</script>

<style>
  .terminal-container {
    width: 100%;
    height: 100vh;
  }
</style>

<div bind:this={termContainer} class="terminal-container"></div>