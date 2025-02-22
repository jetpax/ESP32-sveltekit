<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { socket } from '$lib/stores/socket';
  import { Terminal } from '@xterm/xterm';
  import { FitAddon } from '@xterm/addon-fit';
  import '@xterm/xterm/css/xterm.css';
  import { tick } from 'svelte';


  let term;
  let termContainer;
  let fitAddon;

  // Store command history
  let history: string[] = [];
  let commandIndex = -1;
  let commandBuffer = "";
  let autoScroll = true;

  const banner = [
    "\x1b[38;05;208;1m            ____       __           _    ____  ________     ",
    "       ___ / __ \\___  / /__________| |  / /  |/  / ___/ ___",
    "     ____ / /_/ / _ \\/ __/ ___/ __ \\ | / / /|_/ /\\__ \\/_____",
    "   _____ / _, _/  __/ /_/ /  / /_/ / |/ / /  / /___/ /_______ ",
    "        /_/ |_|\\___/\\__/_/   \\____/|___/_/  /_//____/       ",
    "",
    "\x1b[0;37mVisit: \x1b[1;32mhttps://retrovms.com\x1b[0m\r\n",
  ];

  function prompt() {
    term.write("\r\n>>> ");
  }

  onMount(() => {
    term = new Terminal({
      cursorBlink: true,
      scrollback: 1000,      
      convertEol: true,
      theme: {
        background: '#2e2e2e',
        foreground: '#00ff00',
      }
    });

    fitAddon = new FitAddon();
    term.loadAddon(fitAddon);
    term.open(termContainer);
    fitAddon.fit();

    banner.forEach(line => term.writeln(line));
    prompt();

    let cursorPosition = 0; // Track cursor position

    term.onData((data) => {
      if (data === '\r') { 
        // Enter key pressed
        if (commandBuffer.trim()) {
          history.unshift(commandBuffer);
          commandIndex = -1;
          term.writeln(""); // Move to a new line before execution
          socket.sendEvent("repl", { command: commandBuffer.trim() });
        }
        commandBuffer = ""; // Clear input buffer
        cursorPosition = 0; // Reset cursor position
      } else if (data === '\u007F') { 
        // Backspace key (delete before cursor)
        if (cursorPosition > 0) {
          commandBuffer = commandBuffer.slice(0, cursorPosition - 1) + commandBuffer.slice(cursorPosition);
          cursorPosition--;

          // Rewrite the input line
          term.write('\r>>> ' + commandBuffer + ' ');
          term.write(`\r>>> ${commandBuffer.slice(0, cursorPosition)}`);
        }
      } else if (data === '\u001b[D') { 
        // Left Arrow Key (Move cursor left)
        if (cursorPosition > 0) {
          cursorPosition--;
          term.write('\x1b[D'); // Move cursor left
        }
      } else if (data === '\u001b[C') { 
        // Right Arrow Key (Move cursor right)
        if (cursorPosition < commandBuffer.length) {
          cursorPosition++;
          term.write('\x1b[C'); // Move cursor right
        }
      } else if (data === '\u001b[A') { 
        // Arrow Up (Command History)
        if (history.length > 0) {
          commandIndex = Math.min(commandIndex + 1, history.length - 1);
          commandBuffer = history[commandIndex];
          cursorPosition = commandBuffer.length;

          // Clear and write new input
          term.write(`\r>>> ${' '.repeat(term.cols - 4)}\r>>> ${commandBuffer}`);
        }
      } else if (data === '\u001b[B') { 
        // Arrow Down (Command History)
        if (commandIndex > 0) {
          commandIndex--;
          commandBuffer = history[commandIndex];
          cursorPosition = commandBuffer.length;
        } else {
          commandIndex = -1;
          commandBuffer = "";
          cursorPosition = 0;
        }

        // Clear and write new input
        term.write(`\r>>> ${' '.repeat(term.cols - 4)}\r>>> ${commandBuffer}`);
      } else { 
        // Insert character at cursor position
        commandBuffer = commandBuffer.slice(0, cursorPosition) + data + commandBuffer.slice(cursorPosition);
        cursorPosition++;

        // Rewrite input line properly
        term.write(`\r>>> ${commandBuffer} `);
        term.write(`\r>>> ${commandBuffer.slice(0, cursorPosition)}`);
      }
    });

    socket.on("repl", async (event) => {
    // await tick(); // Ensure Svelte updates before processing

    if (!event) {
        console.error("Invalid REPL response:", event);
        return;
    }

    const atBottom = term.buffer.active.viewportY >= term.buffer.active.baseY - 1;

if (event.stdout) {
    term.write(event.stdout.trim() + "\r\n"); 
}

if (event.result) {
    term.write(event.result.trim());  
}

    prompt(); 

    if (atBottom) {
        term.scrollToBottom();
        autoScroll = true;
    }
});



    term.onScroll(() => {
    // Detect if user has manually scrolled up
    autoScroll = term.buffer.active.baseY === term.buffer.active.viewportY;
    });

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