<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { Terminal } from '@xterm/xterm';
  import { FitAddon } from '@xterm/addon-fit';
  import '@xterm/xterm/css/xterm.css';  // Import default xterm styles

  let term;
  let termContainer;
  let fitAddon;

  onMount(() => {
    term = new Terminal({
      cursorBlink: true,
      theme: {
        background: '#2e2e2e', // Dark grey background
        foreground: '#00ff00', // Green text
      }
    });

    fitAddon = new FitAddon();
    term.loadAddon(fitAddon);
    term.open(termContainer);
    fitAddon.fit();

    // Display welcome banner
    term.write('Welcome to the Berry REPL!\r\n');
    term.write('Type your commands below:\r\n\r\n>>> ');

    // Buffer for command input
    let commandBuffer = '';

    // Handle input from the terminal
    term.onData((data) => {
      switch (data) {
        case '\r': // Enter key
          term.write('\r\n');
          term.write(`Result of "${commandBuffer}"\r\n`); // Echo back the command
          term.write('>>> ');
          commandBuffer = ''; // Clear the command buffer
          break;
        case '\u007F': // Backspace
          if (commandBuffer.length > 0) {
            commandBuffer = commandBuffer.slice(0, -1);
            term.write('\b \b'); // Erase the last character
          }
          break;
        default:
          commandBuffer += data;
          term.write(data); // Echo the typed character
          break;
      }
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